/*
 * libwwn_watchdogd_hook.dylib — DYLD_INSERT into watchdogd (arm64e).
 *
 * 25F80 lessons (post-reboot 2026-08-20):
 * - fishhook GOT patch → SIGBUS 138
 * - dlsym(RTLD_NEXT/DEFAULT) / dlopen(IOKit) → returns the interpose
 *   (hook address). Calling that recurses (Disable path re-enters).
 * - Real original is the __DATA,__interpose replacee pointer.
 * - No FILE I/O inside hooked_iocall (SEGV). Markers from reporter thread.
 * - Never getenv inside hooked_iocall.
 */
#include <IOKit/IOKitLib.h>
#include <errno.h>
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

static iocall_fn g_real = NULL;
static int g_auto_dis = 0;
static volatile int g_armed = 0;
static volatile int g_do_dis = 0;
static volatile int g_done = 0;
static volatile kern_return_t g_dkr = (kern_return_t)-1;
static volatile io_connect_t g_conn = IO_OBJECT_NULL;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static kern_return_t hooked_iocall(mach_port_t connection, uint32_t selector,
                                   const uint64_t *input, uint32_t inputCnt,
                                   uint64_t *output, uint32_t *outputCnt);

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
  if (!g_real)
    return kIOReturnError;

  if (connection != MACH_PORT_NULL && connection != IO_OBJECT_NULL) {
    pthread_mutex_lock(&g_mu);
    g_conn = (io_connect_t)connection;
    pthread_mutex_unlock(&g_mu);
  }

  /* First successful CheckEnabled arm → next call becomes Disable. */
  if (g_do_dis) {
    g_do_dis = 0;
    g_dkr = g_real(connection, kIOWatchdogDaemonDisableUserspaceMonitoring,
                   input, inputCnt, output, outputCnt);
    g_done = 1;
    return g_dkr;
  }

  if (!g_armed && g_auto_dis) {
    g_armed = 1;
    g_do_dis = 1;
  }

  return g_real(connection, selector, input, inputCnt, output, outputCnt);
}

static int call_on_conn(uint32_t selector, char *err, size_t errlen) {
  pthread_mutex_lock(&g_mu);
  io_connect_t c = g_conn;
  pthread_mutex_unlock(&g_mu);
  if (c == IO_OBJECT_NULL || !g_real) {
    snprintf(err, errlen, "no connection");
    return -1;
  }
  uint64_t out[8];
  uint32_t outCnt = 8;
  kern_return_t kr = g_real(c, selector, NULL, 0, out, &outCnt);
  if (kr != KERN_SUCCESS) {
    snprintf(err, errlen, "kr=0x%x", (unsigned)kr);
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
             "OK conn=%s port=0x%x armed=%d do_dis=%d done=%d dkr=0x%x "
             "auto=%d\n",
             c != IO_OBJECT_NULL ? "captured" : "pending", (unsigned)c, g_armed,
             g_do_dis, g_done, (unsigned)g_dkr, g_auto_dis);
  } else if (strcmp(buf, "disable") == 0) {
    /* Direct IOConnect with synthetic buffers returns 0xe00002c2.
     * Arm rewrite: next live CheckEnabled becomes Disable. */
    g_do_dis = 1;
    snprintf(reply, sizeof(reply), "OK armed-next-call\n");
  } else if (strcmp(buf, "enable") == 0) {
    if (call_on_conn(kIOWatchdogDaemonReenableUserspaceMonitoring, err,
                     sizeof(err)) == 0)
      snprintf(reply, sizeof(reply), "OK ReenableUserspaceMonitoring\n");
    else
      snprintf(reply, sizeof(reply), "ERR %s\n", err);
  } else {
    snprintf(reply, sizeof(reply), "ERR unknown\n");
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

static void *reporter_thread(void *arg) {
  (void)arg;
  for (int i = 0; i < 300 && !g_done; i++)
    usleep(20000);
  if (!g_done || g_dkr != KERN_SUCCESS)
    return NULL;
  mkdir("/tmp/libwayland-support", 0755);
  FILE *mf = fopen(WWN_IOW_DISABLED_MARKER, "w");
  if (mf) {
    fputs("path-b-auto\n", mf);
    fclose(mf);
  }
  mkdir(WWN_IOW_DB_DIR, 0755);
  FILE *ok = fopen(WWN_IOW_CLAIM_OK_STAMP, "w");
  if (ok) {
    fputs("ok path=b sticky=1 replacee=1\n", ok);
    fclose(ok);
  }
  unlink(WWN_IOW_CLAIM_PENDING);
  return NULL;
}

__attribute__((constructor)) static void wwn_hook_init(void) {
  /* dyld interpose replacee = real IOConnectCallScalarMethod (not dlsym). */
  g_real = (iocall_fn)g_interpose_iocall.replacee;
  if (g_real == (iocall_fn)hooked_iocall)
    g_real = NULL;

  /* Path B always auto-disables. Env is optional override off. */
  g_auto_dis = 1;
  if (getenv("WWN_IOW_AUTO_DISABLE") != NULL &&
      getenv("WWN_IOW_AUTO_DISABLE")[0] == '0')
    g_auto_dis = 0;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_t th;
  pthread_create(&th, &attr, server_thread, NULL);
  if (g_auto_dis)
    pthread_create(&th, &attr, reporter_thread, NULL);
  pthread_attr_destroy(&attr);
}
