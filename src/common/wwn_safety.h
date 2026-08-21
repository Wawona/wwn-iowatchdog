/*
 * Fail-closed safety guards for Path A / Path B.
 * North star: a live /usr/libexec/watchdogd (or covered successor) at every
 * stable point. Never kickstart -k.
 */
#ifndef WWN_SAFETY_H
#define WWN_SAFETY_H

#include "wwn_iowatchdog.h"

#include <sys/types.h>

#define WWN_IOW_COVERAGE_FAIL WWN_IOW_DB_DIR "/coverage-fail"
#define WWN_IOW_ARM_LOCK WWN_IOW_DB_DIR "/arm.lock"

enum wwn_safety_path {
  WWN_SAFETY_PATH_A = 1,
  WWN_SAFETY_PATH_B = 2,
};

/* pid of /usr/libexec/watchdogd, or -1 */
pid_t wwn_watchdogd_find_pid(void);
int wwn_watchdogd_process_alive(void);

/* Live watchdogd process (Apple or Path B insert into same binary). */
int wwn_coverage_ok(void);

/* launchctl print-disabled: com.apple.watchdogd => disabled */
int wwn_safety_apple_job_disabled(void);

/*
 * If Apple is persist-disabled, a Path A or Path B LaunchDaemon plist must
 * exist so the next boot still gets a watchdogd. Orphan disable = fail.
 */
int wwn_safety_reboot_successor_ok(void);

/* Both Path A and Path B plists present (illegal overlap). */
int wwn_safety_dual_path_armed(void);

/*
 * Path-specific arm preflight. Returns 0 OK, non-zero refuse.
 * Also refuses orphan Apple-disable (use --heal) and dual-path.
 */
int wwn_safety_preflight_arm(enum wwn_safety_path path, const char *hook_path);

/*
 * After arm/uninstall/claim: require process alive; else restore Apple and
 * recheck. Returns 0 OK, non-zero if still uncovered. Writes coverage-fail.
 */
int wwn_safety_postflight(const char *why);

/*
 * Doctor: print state. Exit 0 only if coverage_ok AND reboot_successor_ok
 * AND not dual_path. Non-zero otherwise.
 */
int wwn_safety_doctor(void);

/*
 * Heal: tear down our LaunchDaemons/plists, restore Apple coverage.
 * Keeps claim-ok evidence. Clears pending + coverage-fail on success.
 */
int wwn_safety_heal(void);

/* Exclusive arm/uninstall lock. Returns lock fd (>=0) or -1. */
int wwn_safety_arm_lock(void);
void wwn_safety_arm_unlock(int fd);

int wwn_safety_amfi_relaxed(void);
int wwn_safety_path_a_armed(void);
int wwn_safety_path_b_armed(void);
int wwn_safety_hook_is_arm64e(const char *path);
/* codesign -v --ok; 1 if valid (ad-hoc OK), 0 otherwise */
int wwn_safety_hook_codesign_ok(const char *path);

/* disable + verify print-disabled shows disabled (retry). 0 OK. */
int wwn_watchdogd_job_disable_verified(void);

#endif /* WWN_SAFETY_H */
