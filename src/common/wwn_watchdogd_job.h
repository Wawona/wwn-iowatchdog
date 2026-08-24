/*
 * Persist-enable/disable of com.apple.watchdogd for the Path A claim boot.
 * Never use kickstart -k (kills a live daemon). kickstart without -k only
 * starts when not running.
 */
#ifndef WWN_WATCHDOGD_JOB_H
#define WWN_WATCHDOGD_JOB_H

#define WWN_APPLE_WATCHDOGD_JOB "system/com.apple.watchdogd"
#define WWN_IOW_DB_DIR "/var/db/wwn-iowatchdog"
#define WWN_IOW_CLAIM_PENDING WWN_IOW_DB_DIR "/claim-pending"
#define WWN_IOW_CLAIM_OK_STAMP WWN_IOW_DB_DIR "/claim-ok"
#define WWN_CLAIM_PLIST \
  "/Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-claim.plist"
#define WWN_RESTORE_PLIST \
  "/Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-restore.plist"
/* Durable install (nix /tmp result links die across reboot). */
#define WWN_IOW_INSTALL_DIR \
  "/Library/Application Support/Wawona/wwn-iowatchdog"
#define WWN_IOW_INSTALLED_CLAIM WWN_IOW_INSTALL_DIR "/wwn-iowatchdog-claim"
#define WWN_IOW_INSTALLED_CLI WWN_IOW_INSTALL_DIR "/wwn-iowatchdog"
#define WWN_IOW_INSTALLED_HOOK WWN_IOW_INSTALL_DIR "/libwwn_watchdogd_hook.dylib"
#define WWN_IOW_INSTALLED_WRAPPER WWN_IOW_INSTALL_DIR "/watchdogd-pathb-wrapper.sh"
#define WWN_PATHB_PLIST \
  "/Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-pathb.plist"

/* 0 = ok */
int wwn_watchdogd_job_disable(void);
int wwn_watchdogd_job_enable(void);
/* Start if not running. Never passes -k. */
int wwn_watchdogd_job_kickstart(void);
/*
 * Enable Apple's job and get a *stable* /usr/libexec/watchdogd.
 * macOS 26 KeepAlive SuccessfulExit=false plus IOKit LaunchEvents: a
 * kickstart without -k is a oneshot (exit 0, no restart). Restore
 * re-registers LaunchEvents (bootout+bootstrap) only while no
 * watchdogd is live. Never kickstart -k. Never bootout a live daemon.
 */
int wwn_watchdogd_job_restore(void);
/* Wait until /usr/libexec/watchdogd is gone. 0 = gone, -1 = still alive. */
int wwn_watchdogd_wait_gone(unsigned timeout_ms);

int wwn_iow_db_mkdir(void);
int wwn_iow_write_file(const char *path, const char *text);
void wwn_iow_unlink_quiet(const char *path);

#endif
