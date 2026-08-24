#include "wwn_watchdogd_job.h"
#include "wwn_safety.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int run_cmd(const char *cmd) {
  int st = system(cmd);
  if (st == -1)
    return -1;
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  return -1;
}

int wwn_iow_db_mkdir(void) {
  if (mkdir(WWN_IOW_DB_DIR, 0755) == 0)
    return 0;
  return errno == EEXIST ? 0 : -1;
}

int wwn_iow_write_file(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;
  if (text)
    fputs(text, f);
  fclose(f);
  return 0;
}

void wwn_iow_unlink_quiet(const char *path) { (void)unlink(path); }

int wwn_watchdogd_job_disable(void) {
  /* Persists across reboot. Claim opens IOWatchdog before Apple's job. */
  return run_cmd("/bin/launchctl disable " WWN_APPLE_WATCHDOGD_JOB);
}

int wwn_watchdogd_job_enable(void) {
  return run_cmd("/bin/launchctl enable " WWN_APPLE_WATCHDOGD_JOB);
}

int wwn_watchdogd_job_kickstart(void) {
  /* No -k: never kill a live watchdogd. */
  return run_cmd("/bin/launchctl kickstart " WWN_APPLE_WATCHDOGD_JOB);
}

int wwn_watchdogd_wait_gone(unsigned timeout_ms) {
  unsigned slept = 0;
  for (;;) {
    if (!wwn_watchdogd_process_alive())
      return 0;
    if (slept >= timeout_ms)
      return -1;
    usleep(100000);
    slept += 100;
  }
}

/* Require the process to stay up. kickstart oneshots die within this window. */
static int watchdogd_stayed_alive(unsigned dwell_ms) {
  unsigned slept = 0;
  while (slept < dwell_ms) {
    usleep(100000);
    slept += 100;
    if (!wwn_watchdogd_process_alive())
      return 0;
  }
  return 1;
}

int wwn_watchdogd_job_restore(void) {
  int e = wwn_watchdogd_job_enable();

  /* Live coverage: never bootout Apple (that SIGTERMs watchdogd). */
  if (wwn_watchdogd_process_alive()) {
    if (watchdogd_stayed_alive(1500))
      return (e != 0) ? e : 0;
  }

  /*
   * Apple's plist starts from IOKit LaunchEvents, not KeepAlive=true.
   * SuccessfulExit=false: kickstart without -k runs once, watchdogd
   * exits 0, launchd does not restart it. Heal then reported OK while
   * doctor saw no process. Re-register LaunchEvents only when the
   * process is already gone.
   */
  if (!wwn_watchdogd_process_alive()) {
    (void)run_cmd("/bin/launchctl bootout " WWN_APPLE_WATCHDOGD_JOB
                  " 2>/dev/null");
    (void)run_cmd("/bin/launchctl bootstrap system "
                  "/System/Library/LaunchDaemons/com.apple.watchdogd.plist "
                  "2>/dev/null");
    for (int i = 0; i < 25; i++) {
      if (wwn_watchdogd_process_alive() && watchdogd_stayed_alive(1500))
        return (e != 0) ? e : 0;
      usleep(200000);
    }
  }

  int last_k = -1;
  for (int i = 0; i < 10; i++) {
    last_k = wwn_watchdogd_job_kickstart();
    if (wwn_watchdogd_process_alive() && watchdogd_stayed_alive(1500))
      return (e != 0) ? e : 0;
    usleep(200000);
  }
  fprintf(stderr,
          "wwn-watchdogd-job: restore: /usr/libexec/watchdogd did not stay "
          "running after LaunchEvents re-register + kickstart (no -k). "
          "Restart this Mac. Do not kickstart -k.\n");
  return (e != 0) ? e : (last_k != 0 ? last_k : 1);
}
