/*
 * libwwn_watchdogd_hook.dylib — loaded into live watchdogd (arm64e).
 *
 * Captures the exclusive IOWatchdog io_connect_t from IOConnectCallScalarMethod
 * checkins, then serves disable/enable over /var/run/wwn-iowatchdog.sock.
 *
 * Never attach lldb. Constructor must not crash (would panic the machine).
 */
#include <IOKit/IOKitLib.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define WWN_IOW_SOCK_PATH "/var/run/wwn-iowatchdog.sock"

enum {
  kIOWatchdogDaemonCheckEnabled = 0,
  kIOWatchdogDaemonCheckUserspaceDefanged = 1,
  kIOWatchdogDaemonCheckin = 2,
  kIOWatchdogDaemonDisableUserspaceMonitoring = 3,
  kIOWatchdogDaemonReenableUserspaceMonitoring = 4,
};

typedef kern_return_t (*iocall_fn)(mach_port_t connection, uint32_t selector,
                                   const uint64_t *input,
                                   uint32_t inputCnt, uint64_t *output,
                                   uint32_t *outputCnt);

static iocall_fn g_orig_iocall = NULL;
static io_connect_t g_conn = IO_OBJECT_NULL;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_sock_fd = -1;
static volatile int g_server_started = 0;

/* ---- minimal fishhook (rebind one symbol in loaded images) ---- */

struct wwn_rebinding {
  const char *name;
  void *replacement;
  void **replaced;
};

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#ifdef __LP64__
typedef struct mach_header_64 wwn_mh_t;
typedef struct segment_command_64 wwn_seg_t;
typedef struct section_64 wwn_sec_t;
typedef struct nlist_64 wwn_nlist_t;
#define WWN_LC_SEGMENT LC_SEGMENT_64
#else
typedef struct mach_header wwn_mh_t;
typedef struct segment_command wwn_seg_t;
typedef struct section wwn_sec_t;
typedef struct nlist wwn_nlist_t;
#define WWN_LC_SEGMENT LC_SEGMENT
#endif

static void wwn_rebind_image(const struct mach_header *mh, intptr_t slide,
                             struct wwn_rebinding *rebindings, size_t ne) {
  Dl_info info;
  if (dladdr(mh, &info) == 0)
    return;

  const wwn_mh_t *header = (const wwn_mh_t *)mh;
  uintptr_t cur = (uintptr_t)(header + 1);
  uintptr_t end = cur + header->sizeofcmds;
  wwn_seg_t *linkedit = NULL;
  struct symtab_command *symtab = NULL;
  struct dysymtab_command *dysym = NULL;

  while (cur < end) {
    struct load_command *lc = (struct load_command *)cur;
    if (lc->cmd == WWN_LC_SEGMENT) {
      wwn_seg_t *seg = (wwn_seg_t *)cur;
      if (strcmp(seg->segname, SEG_LINKEDIT) == 0)
        linkedit = seg;
    } else if (lc->cmd == LC_SYMTAB) {
      symtab = (struct symtab_command *)cur;
    } else if (lc->cmd == LC_DYSYMTAB) {
      dysym = (struct dysymtab_command *)cur;
    }
    cur += lc->cmdsize;
  }
  if (!linkedit || !symtab || !dysym)
    return;

  uintptr_t linkedit_base =
      (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
  wwn_nlist_t *sym =
      (wwn_nlist_t *)(linkedit_base + symtab->symoff);
  char *strtab = (char *)(linkedit_base + symtab->stroff);
  uint32_t *indirect =
      (uint32_t *)(linkedit_base + dysym->indirectsymoff);

  cur = (uintptr_t)(header + 1);
  while (cur < end) {
    struct load_command *lc = (struct load_command *)cur;
    if (lc->cmd == WWN_LC_SEGMENT) {
      wwn_seg_t *seg = (wwn_seg_t *)cur;
      wwn_sec_t *secs = (wwn_sec_t *)(seg + 1);
      for (uint32_t i = 0; i < seg->nsects; i++) {
        wwn_sec_t *sec = &secs[i];
        uint32_t flags = sec->flags & SECTION_TYPE;
        if (flags != S_LAZY_SYMBOL_POINTERS &&
            flags != S_NON_LAZY_SYMBOL_POINTERS)
          continue;
        uint32_t *isym = indirect + sec->reserved1;
        void **bindings = (void **)((uintptr_t)slide + sec->addr);
        uint32_t n = (uint32_t)(sec->size / sizeof(void *));
        for (uint32_t j = 0; j < n; j++) {
          uint32_t symIndex = isym[j];
          if (symIndex == INDIRECT_SYMBOL_ABS ||
              symIndex == INDIRECT_SYMBOL_LOCAL ||
              symIndex == (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS))
            continue;
          uint32_t strx = sym[symIndex].n_un.n_strx;
          const char *name = strtab + strx;
          if (name[0] == '_')
            name++;
          for (size_t r = 0; r < ne; r++) {
            if (strcmp(name, rebindings[r].name) != 0)
              continue;
            if (rebindings[r].replaced != NULL &&
                *(rebindings[r].replaced) == NULL)
              *(rebindings[r].replaced) = bindings[j];
            bindings[j] = rebindings[r].replacement;
          }
        }
      }
    }
    cur += lc->cmdsize;
  }
}

static void wwn_rebind_symbols(struct wwn_rebinding *rebindings, size_t ne) {
  uint32_t count = _dyld_image_count();
  for (uint32_t i = 0; i < count; i++) {
    wwn_rebind_image(_dyld_get_image_header(i),
                     _dyld_get_image_vmaddr_slide(i), rebindings, ne);
  }
}

/* ---- IOConnect hook ---- */

static kern_return_t hooked_iocall(mach_port_t connection, uint32_t selector,
                                   const uint64_t *input, uint32_t inputCnt,
                                   uint64_t *output, uint32_t *outputCnt) {
  if (connection != MACH_PORT_NULL && connection != IO_OBJECT_NULL) {
    pthread_mutex_lock(&g_mu);
    g_conn = (io_connect_t)connection;
    pthread_mutex_unlock(&g_mu);
  }
  if (!g_orig_iocall)
    return kIOReturnError;
  return g_orig_iocall(connection, selector, input, inputCnt, output,
                       outputCnt);
}

static int call_on_conn(uint32_t selector, char *err, size_t errlen) {
  pthread_mutex_lock(&g_mu);
  io_connect_t c = g_conn;
  pthread_mutex_unlock(&g_mu);
  if (c == IO_OBJECT_NULL) {
    snprintf(err, errlen, "no connection captured yet");
    return -1;
  }
  kern_return_t kr =
      g_orig_iocall ? g_orig_iocall(c, selector, NULL, 0, NULL, NULL)
                    : IOConnectCallScalarMethod(c, selector, NULL, 0, NULL,
                                                NULL);
  if (kr != KERN_SUCCESS) {
    snprintf(err, errlen, "%s (0x%x)", mach_error_string(kr), (unsigned)kr);
    return -1;
  }
  return 0;
}

static void handle_client(int fd) {
  char buf[256];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0)
    return;
  buf[n] = '\0';
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[--n] = '\0';

  char err[128];
  char reply[256];

  if (strcmp(buf, "ping") == 0) {
    snprintf(reply, sizeof(reply), "OK pong\n");
  } else if (strcmp(buf, "status") == 0) {
    pthread_mutex_lock(&g_mu);
    io_connect_t c = g_conn;
    pthread_mutex_unlock(&g_mu);
    snprintf(reply, sizeof(reply),
             "OK conn=%s port=0x%x\n",
             c != IO_OBJECT_NULL ? "captured" : "pending", (unsigned)c);
  } else if (strcmp(buf, "disable") == 0) {
    if (call_on_conn(kIOWatchdogDaemonDisableUserspaceMonitoring, err,
                     sizeof(err)) == 0)
      snprintf(reply, sizeof(reply), "OK DisableUserspaceMonitoring\n");
    else
      snprintf(reply, sizeof(reply), "ERR %s\n", err);
  } else if (strcmp(buf, "enable") == 0) {
    if (call_on_conn(kIOWatchdogDaemonReenableUserspaceMonitoring, err,
                     sizeof(err)) == 0)
      snprintf(reply, sizeof(reply), "OK ReenableUserspaceMonitoring\n");
    else
      snprintf(reply, sizeof(reply), "ERR %s\n", err);
  } else {
    snprintf(reply, sizeof(reply), "ERR unknown command\n");
  }
  send(fd, reply, strlen(reply), 0);
}

static void *server_thread(void *arg) {
  (void)arg;
  unlink(WWN_IOW_SOCK_PATH);
  int s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s < 0)
    return NULL;
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, WWN_IOW_SOCK_PATH, sizeof(addr.sun_path) - 1);
  if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(s);
    return NULL;
  }
  chmod(WWN_IOW_SOCK_PATH, 0600);
  if (listen(s, 4) != 0) {
    close(s);
    unlink(WWN_IOW_SOCK_PATH);
    return NULL;
  }
  g_sock_fd = s;
  g_server_started = 1;
  for (;;) {
    int c = accept(s, NULL, NULL);
    if (c < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    handle_client(c);
    close(c);
  }
  return NULL;
}

__attribute__((constructor)) static void wwn_hook_init(void) {
  /*
   * Fail soft: never abort. A constructor crash inside watchdogd panics
   * the machine (PanicOnConsecutiveCrash).
   */
  void *sym = dlsym(RTLD_DEFAULT, "IOConnectCallScalarMethod");
  if (!sym)
    return;

  struct wwn_rebinding rebindings[1] = {
      {"IOConnectCallScalarMethod", (void *)hooked_iocall,
       (void **)&g_orig_iocall},
  };
  wwn_rebind_symbols(rebindings, 1);
  if (!g_orig_iocall)
    g_orig_iocall = (iocall_fn)sym;

  pthread_t th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&th, &attr, server_thread, NULL) != 0) {
    pthread_attr_destroy(&attr);
    return;
  }
  pthread_attr_destroy(&attr);
}
