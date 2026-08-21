/*
 * Opt-in Path A claim: open IOWatchdog type=1 with
 * com.apple.private.iowatchdog.user-access, DisableUserspaceMonitoring.
 *
 * Boot automation (25F80): claim-install persist-disables com.apple.watchdogd
 * so this binary wins exclusive open at RunAtLoad. After sticky disable it
 * restores Apple's job (enable + kickstart without -k). A restore LaunchDaemon
 * is a safety net if this process dies mid-flight.
 *
 * AMFI: ad-hoc private entitlement needs amfi_get_out_of_my_way=1 or the
 * process dies OS_REASON_CODESIGNING / SIGKILL 137 before main().
 *
 * Default: sticky-release (disable, close, exit). --hold keeps exclusive.
 */
#include "../common/wwn_iowatchdog.h"
#include "../common/wwn_safety.h"
#include "../common/wwn_watchdogd_job.h"

#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <mach/mach_error.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
  (void)sig;
  g_stop = 1;
}

static int bootargs_has_amfi_relaxed(void) {
  return wwn_safety_amfi_relaxed();
}

/*
 * Restore Apple job and assert a live process. Does not clear claim-ok on
 * failure (evidence preserved). Writes coverage-fail if still dead.
 * Returns 0 if covered, non-zero otherwise.
 */
static int restore_apple_watchdogd(const char *why) {
  fprintf(stderr, "wwn-iowatchdog-claim: restore com.apple.watchdogd (%s)\n",
          why ? why : "");
  if (wwn_watchdogd_job_restore() != 0)
    fprintf(stderr,
            "wwn-iowatchdog-claim: WARNING: restore cmd failed; retry once\n");
  if (!wwn_watchdogd_process_alive()) {
    (void)wwn_watchdogd_job_restore();
  }
  if (wwn_safety_postflight(why ? why : "claim-restore") != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim: HARD FAIL: no live watchdogd after restore\n"
            "  stamp: %s\n"
            "  claim-ok left intact if present\n",
            WWN_IOW_COVERAGE_FAIL);
    return 1;
  }
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
  return 0;
}

static void write_ok_stamp(void) {
  char buf[160];
  time_t now = time(NULL);
  snprintf(buf, sizeof(buf), "ok path=a sticky=1 time=%ld\n", (long)now);
  (void)wwn_iow_db_mkdir();
  (void)wwn_iow_write_file(WWN_IOW_CLAIM_OK_STAMP, buf);
}

static kern_return_t call_disable(io_connect_t conn) {
  /* 25F80: non-zero outputCnt → 0xe00002c2; outCnt=0 + NULL out succeeds. */
  uint32_t outCnt = 0;
  return IOConnectCallScalarMethod(
      conn, kIOWatchdogDaemonDisableUserspaceMonitoring, NULL, 0, NULL,
      &outCnt);
}

int main(int argc, char **argv) {
  int hold = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--hold") == 0)
      hold = 1;
    else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      fprintf(stderr,
              "usage: wwn-iowatchdog-claim [--hold]\n"
              "  Path A: entitled IOServiceOpen(type=1) + Disable (sel 3).\n"
              "  Needs amfi_get_out_of_my_way=1 for ad-hoc private entitlement.\n"
              "  default: disable, close, restore watchdogd (sticky)\n"
              "  --hold: keep exclusive until SIGTERM, then restore\n");
      return 0;
    }
  }

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim: must run as root\n");
    return 1;
  }

  if (!bootargs_has_amfi_relaxed()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim: WARNING: kern.bootargs lacks "
            "amfi_get_out_of_my_way=1.\n"
            "  Ad-hoc com.apple.private.iowatchdog.user-access usually dies "
            "with OS_REASON_CODESIGNING / 137 before this message.\n"
            "  Set via: sudo wwn-iowatchdog-claim-install --path-a-amfi-nvram\n"
            "  then reboot. Prefer Path B if AMFI must stay on.\n");
  }

  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);

  (void)wwn_iow_db_mkdir();
  (void)wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "path-a\n");

  io_service_t svc = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching(WWN_IOW_SERVICE_NAME));
  if (svc == IO_OBJECT_NULL) {
    fprintf(stderr, "wwn-iowatchdog-claim: no IOWatchdog service\n");
    (void)restore_apple_watchdogd("no-service");
    return 2;
  }
  io_connect_t conn = IO_OBJECT_NULL;
  kern_return_t kr =
      IOServiceOpen(svc, mach_task_self(), WWN_IOW_OPEN_TYPE, &conn);
  IOObjectRelease(svc);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog-claim: IOServiceOpen: %s (0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    (void)restore_apple_watchdogd("open-failed");
    return 3;
  }

  kr = call_disable(conn);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr,
            "wwn-iowatchdog-claim: DisableUserspaceMonitoring: %s (0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    IOServiceClose(conn);
    (void)restore_apple_watchdogd("disable-failed");
    return 4;
  }

  mkdir("/tmp/libwayland-support", 0755);
  FILE *mf = fopen(WWN_IOW_DISABLED_MARKER, "w");
  if (mf) {
    fputs(hold ? "path-a-hold\n" : "path-a-sticky\n", mf);
    fclose(mf);
  }
  FILE *cf = fopen(WWN_IOW_CLAIM_MARKER, "w");
  if (cf) {
    fprintf(cf, "pid=%d mode=%s path=a\n", (int)getpid(),
            hold ? "hold" : "sticky-release");
    fclose(cf);
  }
  write_ok_stamp();

  if (!hold) {
    IOServiceClose(conn);
    fprintf(stderr,
            "wwn-iowatchdog-claim: Path A disable ACK; closed exclusive "
            "(sticky-after-close). pid=%d\n",
            (int)getpid());
    return restore_apple_watchdogd("sticky-ok") ? 10 : 0;
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim: holding exclusive IOWatchdog (Path A). "
          "pid=%d\n",
          (int)getpid());

  while (!g_stop)
    sleep(1);

  IOServiceClose(conn);
  unlink(WWN_IOW_CLAIM_MARKER);
  fprintf(stderr,
          "wwn-iowatchdog-claim: released exclusive; monitoring left disabled "
          "(sticky)\n");
  return restore_apple_watchdogd("hold-exit") ? 10 : 0;
}
