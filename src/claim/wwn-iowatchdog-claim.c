/*
 * Opt-in claim helper: open IOWatchdog type=1, DisableUserspaceMonitoring.
 *
 * Default (sticky-release, from 25F80 kext RE): after disable ACK, close the
 * client and exit. userClientClose clears the exclusive slot (+0x98) but does
 * NOT restore the monitoring flag (+0xa8). checkWatchdog early-outs when
 * +0xa8 is clear. So monitoring stays off and watchdogd may open later.
 *
 * --hold: keep the exclusive connection until SIGTERM (legacy race hold).
 * Do not re-enable on exit in either mode (sticky is intentional).
 *
 * Boot race: must win open before watchdogd. Install via claim-install and
 * reboot. Never unload watchdogd without a disable ACK.
 */
#include "../common/wwn_iowatchdog.h"

#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <mach/mach_error.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

int main(int argc, char **argv) {
  int hold = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--hold") == 0)
      hold = 1;
    else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
              "usage: wwn-iowatchdog-claim [--hold]\n"
              "  default: disable, close, exit (sticky-after-close)\n"
              "  --hold: keep exclusive until SIGTERM\n");
      return 0;
    }
  }

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim: must run as root\n");
    return 1;
  }

  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);

  io_service_t svc = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching(WWN_IOW_SERVICE_NAME));
  if (svc == IO_OBJECT_NULL) {
    fprintf(stderr, "wwn-iowatchdog-claim: no IOWatchdog service\n");
    return 2;
  }
  io_connect_t conn = IO_OBJECT_NULL;
  kern_return_t kr =
      IOServiceOpen(svc, mach_task_self(), WWN_IOW_OPEN_TYPE, &conn);
  IOObjectRelease(svc);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog-claim: IOServiceOpen: %s (0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    return 3;
  }

  kr = IOConnectCallScalarMethod(
      conn, kIOWatchdogDaemonDisableUserspaceMonitoring, NULL, 0, NULL, NULL);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog-claim: DisableUserspaceMonitoring: %s "
                    "(0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    IOServiceClose(conn);
    return 4;
  }

  mkdir("/tmp/libwayland-support", 0755);
  FILE *mf = fopen(WWN_IOW_DISABLED_MARKER, "w");
  if (mf) {
    fputs(hold ? "claim-hold\n" : "claim-sticky\n", mf);
    fclose(mf);
  }
  FILE *cf = fopen(WWN_IOW_CLAIM_MARKER, "w");
  if (cf) {
    fprintf(cf, "pid=%d mode=%s\n", (int)getpid(),
            hold ? "hold" : "sticky-release");
    fclose(cf);
  }

  if (!hold) {
    /* Sticky: close without ReenableUserspaceMonitoring. */
    IOServiceClose(conn);
    fprintf(stderr,
            "wwn-iowatchdog-claim: disable ACK; closed exclusive "
            "(sticky-after-close). pid=%d\n",
            (int)getpid());
    return 0;
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim: holding exclusive IOWatchdog (disable ACK). "
          "pid=%d\n",
          (int)getpid());

  while (!g_stop)
    sleep(1);

  /* Hold mode exit: still do NOT re-enable (sticky intentional). */
  IOServiceClose(conn);
  unlink(WWN_IOW_CLAIM_MARKER);
  fprintf(stderr,
          "wwn-iowatchdog-claim: released exclusive; monitoring left disabled "
          "(sticky)\n");
  return 0;
}
