/*
 * Opt-in claim helper: open IOWatchdog type=1, DisableUserspaceMonitoring.
 *
 * Boot automation (25F80): claim-install persist-disables com.apple.watchdogd
 * so this binary wins exclusive open at RunAtLoad. After sticky disable it
 * restores Apple's job (enable + kickstart without -k). A restore LaunchDaemon
 * is a safety net if this process dies mid-flight.
 *
 * Default: sticky-release (disable, close, exit). --hold keeps exclusive.
 */
#include "../common/wwn_iowatchdog.h"
#include "../common/wwn_watchdogd_job.h"

#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <mach/mach_error.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static void restore_apple_watchdogd(const char *why) {
  fprintf(stderr, "wwn-iowatchdog-claim: restore com.apple.watchdogd (%s)\n",
          why ? why : "");
  if (wwn_watchdogd_job_restore() != 0)
    fprintf(stderr,
            "wwn-iowatchdog-claim: WARNING: restore failed; run:\n"
            "  sudo launchctl enable system/com.apple.watchdogd\n"
            "  sudo launchctl kickstart system/com.apple.watchdogd\n");
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
}

static void write_ok_stamp(void) {
  char buf[128];
  time_t now = time(NULL);
  snprintf(buf, sizeof(buf), "ok time=%ld sticky=1\n", (long)now);
  (void)wwn_iow_db_mkdir();
  (void)wwn_iow_write_file(WWN_IOW_CLAIM_OK_STAMP, buf);
}

int main(int argc, char **argv) {
  int hold = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--hold") == 0)
      hold = 1;
    else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
              "usage: wwn-iowatchdog-claim [--hold]\n"
              "  default: disable, close, restore watchdogd (sticky)\n"
              "  --hold: keep exclusive until SIGTERM, then restore\n");
      return 0;
    }
  }

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim: must run as root\n");
    return 1;
  }

  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);

  (void)wwn_iow_db_mkdir();
  (void)wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "1\n");

  io_service_t svc = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching(WWN_IOW_SERVICE_NAME));
  if (svc == IO_OBJECT_NULL) {
    fprintf(stderr, "wwn-iowatchdog-claim: no IOWatchdog service\n");
    restore_apple_watchdogd("no-service");
    return 2;
  }
  io_connect_t conn = IO_OBJECT_NULL;
  kern_return_t kr =
      IOServiceOpen(svc, mach_task_self(), WWN_IOW_OPEN_TYPE, &conn);
  IOObjectRelease(svc);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog-claim: IOServiceOpen: %s (0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    restore_apple_watchdogd("open-failed");
    return 3;
  }

  kr = IOConnectCallScalarMethod(
      conn, kIOWatchdogDaemonDisableUserspaceMonitoring, NULL, 0, NULL, NULL);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog-claim: DisableUserspaceMonitoring: %s "
                    "(0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    IOServiceClose(conn);
    restore_apple_watchdogd("disable-failed");
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
  write_ok_stamp();

  if (!hold) {
    IOServiceClose(conn);
    fprintf(stderr,
            "wwn-iowatchdog-claim: disable ACK; closed exclusive "
            "(sticky-after-close). pid=%d\n",
            (int)getpid());
    /* Sticky ACK held: safe to bring Apple's watchdogd back. */
    restore_apple_watchdogd("sticky-ok");
    return 0;
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim: holding exclusive IOWatchdog (disable ACK). "
          "pid=%d\n",
          (int)getpid());

  while (!g_stop)
    sleep(1);

  IOServiceClose(conn);
  unlink(WWN_IOW_CLAIM_MARKER);
  fprintf(stderr,
          "wwn-iowatchdog-claim: released exclusive; monitoring left disabled "
          "(sticky)\n");
  restore_apple_watchdogd("hold-exit");
  return 0;
}
