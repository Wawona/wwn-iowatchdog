#include "wwn_watchdogd_job.h"

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

int wwn_watchdogd_job_restore(void) {
  int e = wwn_watchdogd_job_enable();
  /* After persist-disable, the job may be absent from the domain. */
  (void)run_cmd("/bin/launchctl bootstrap system "
                "/System/Library/LaunchDaemons/com.apple.watchdogd.plist "
                "2>/dev/null");
  int k = wwn_watchdogd_job_kickstart();
  return (e != 0) ? e : k;
}
