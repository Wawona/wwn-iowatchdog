/*
 * Opt-in claim daemon: open IOWatchdog type=1, DisableUserspaceMonitoring,
 * then hold the exclusive connection until SIGTERM.
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
  (void)argc;
  (void)argv;
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
    fputs("claim\n", mf);
    fclose(mf);
  }
  FILE *cf = fopen(WWN_IOW_CLAIM_MARKER, "w");
  if (cf) {
    fprintf(cf, "pid=%d\n", (int)getpid());
    fclose(cf);
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim: holding exclusive IOWatchdog (disable ACK). "
          "pid=%d\n",
          (int)getpid());

  while (!g_stop)
    sleep(1);

  /* Re-enable before release so a later watchdogd open is not racing a
   * sticky-unknown state. Sticky-after-close is unproven on 25F80 kext. */
  (void)IOConnectCallScalarMethod(
      conn, kIOWatchdogDaemonReenableUserspaceMonitoring, NULL, 0, NULL, NULL);
  IOServiceClose(conn);
  unlink(WWN_IOW_CLAIM_MARKER);
  unlink(WWN_IOW_DISABLED_MARKER);
  fprintf(stderr, "wwn-iowatchdog-claim: released\n");
  return 0;
}
