/*
 * libwwn_watchdogd_hook.dylib — loaded into watchdogd via DYLD_INSERT
 * (arm64e). Uses dyld __DATA,__interpose (NOT fishhook GOT patch).
 *
 * 25F80: fishhook rebind caused SIGBUS 138 on insert. Minimal ctor +
 * DYLD_INTERPOSE loads cleanly under -arm64e_preview_abi.
 *
 * Captures IOWatchdog io_connect_t from IOConnectCallScalarMethod, serves
 * disable/enable on /var/run/wwn-iowatchdog.sock. Optional sticky auto-
 * disable after Checkin when WWN_IOW_AUTO_DISABLE=1.
 *
 * Never attach lldb. Constructor must not crash (PanicOnConsecutiveCrash).
 */
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <errno.h>
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

#include "../common/wwn_iowatchdog.h"
#include "../common/wwn_watchdogd_job.h"

typedef kern_return_t (*iocall_fn)(mach_port_t connection, uint32_t selector,
                                   const uint64_t *input, uint32_t inputCnt,
                                   uint64_t *output, uint32_t *outputCnt);

static iocall_fn g_orig_iocall = NULL;
static io_connect_t g_conn = IO_OBJECT_NULL;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static kern_return_t hooked_iocall(mach_port_t connection, uint32_t selector,
                                   const uint64_t *input, uint32_t inputCnt,
                                   uint64_t *output, uint32_t *outputCnt);

/*
 * dyld interpose: arm64e-safe. Do not fishhook-patch PAC'd GOT slots.
 * Call the real symbol via g_orig_iocall from dlsym(RTLD_NEXT), never by
 * the interposed name (would recurse).
 */
typedef struct {
  const void *replacement;
  const void *replacee;
} wwn_interpose_t;

__attribute__((used, section("__DATA,__interpose"))) static wwn_interpose_t
    g_interpose_iocall = {(const void *)hooked_iocall,
                          (const void *)IOConnectCallScalarMethod};

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
  kern_return_t kr = g_orig_iocall(connection, selector, input, inputCnt,
                                   output, outputCnt);

  static volatile int auto_done = 0;
  if (kr == KERN_SUCCESS &&
      selector == (uint32_t)kIOWatchdogDaemonCheckin && !auto_done &&
      getenv("WWN_IOW_AUTO_DISABLE") != NULL) {
    auto_done = 1;
    kern_return_t dkr = g_orig_iocall(
        connection, kIOWatchdogDaemonDisableUserspaceMonitoring, NULL, 0,
        NULL, NULL);
    if (dkr == KERN_SUCCESS) {
      mkdir("/tmp/libwayland-support", 0755);
      FILE *mf = fopen(WWN_IOW_DISABLED_MARKER, "w");
      if (mf) {
        fputs("path-b-auto\n", mf);
        fclose(mf);
      }
      mkdir(WWN_IOW_DB_DIR, 0755);
      FILE *ok = fopen(WWN_IOW_CLAIM_OK_STAMP, "w");
      if (ok) {
        fputs("ok path=b sticky=1 interpose=1\n", ok);
        fclose(ok);
      }
      unlink(WWN_IOW_CLAIM_PENDING);
    }
  }
  return kr;
}

static int call_on_conn(uint32_t selector, char *err, size_t errlen) {
  pthread_mutex_lock(&g_mu);
  io_connect_t c = g_conn;
  pthread_mutex_unlock(&g_mu);
  if (c == IO_OBJECT_NULL) {
    snprintf(err, errlen, "no connection captured yet");
    return -1;
  }
  if (!g_orig_iocall) {
    snprintf(err, errlen, "no orig IOConnectCallScalarMethod");
    return -1;
  }
  kern_return_t kr = g_orig_iocall(c, selector, NULL, 0, NULL, NULL);
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
    snprintf(reply, sizeof(reply), "OK conn=%s port=0x%x\n",
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
   * Resolve the real IOConnect before any interposed call. Fail soft.
   */
  g_orig_iocall =
      (iocall_fn)dlsym(RTLD_NEXT, "IOConnectCallScalarMethod");
  if (!g_orig_iocall)
    g_orig_iocall =
        (iocall_fn)dlsym(RTLD_DEFAULT, "IOConnectCallScalarMethod");

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
