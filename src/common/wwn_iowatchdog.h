/*
 * Shared constants for wwn-iowatchdog (CLI + hook + inject + claim).
 * Desktop Mode B only. Never ship on Apple mobile.
 *
 * Selectors verified on macOS 26 / 25F80 via llvm-objdump of
 * /usr/libexec/watchdogd (arm64e). Disable=3 and Reenable=4 match
 * historical FBVNC / who_let_the_dogs_out. Checkin is 1 on 25F80
 * (not 2).
 */
#ifndef WWN_IOWATCHDOG_H
#define WWN_IOWATCHDOG_H

#include <mach/mach.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define WWN_IOW_SOCK_PATH "/var/run/wwn-iowatchdog.sock"
#define WWN_IOW_HOOK_NAME "libwwn_watchdogd_hook.dylib"
#define WWN_IOW_DISABLED_MARKER \
  "/tmp/libwayland-support/iowatchdog-userspace-disabled"
#define WWN_IOW_CLAIM_MARKER "/var/run/wwn-iowatchdog-claim.held"
#define WWN_IOW_SERVICE_NAME "IOWatchdog"
#define WWN_IOW_OPEN_TYPE 1u

/* IOWatchdogDaemonUserClientMethod (25F80 watchdogd). */
enum {
  kIOWatchdogDaemonCheckEnabled = 0,
  kIOWatchdogDaemonCheckin = 1,
  /* Historical docs used Checkin=2; 25F80 binary uses 1. Keep gap unused. */
  kIOWatchdogDaemonDisableUserspaceMonitoring = 3,
  kIOWatchdogDaemonReenableUserspaceMonitoring = 4,
  kIOWatchdogDaemonCheckUserspaceDefanged = 5,
};

/* Path A: entitled direct open. Returns 0 OK, 1 exclusive, <0 other. */
int wwn_direct_scalar(uint32_t selector);
int wwn_direct_open_probe(void);

/* Path B: Unix socket to in-watchdogd hook. */
int wwn_sock_cmd(const char *cmd, char *reply, size_t reply_len);
int wwn_sock_present(void);

/* Port find / legacy soft-inject (fail-closed on 25F80). */
int wwn_find_iowatchdog_port_name(pid_t pid, mach_port_name_t *out_name);
int wwn_remote_ioconnect_scalar(pid_t pid, mach_port_name_t port_name,
                                uint32_t selector);
int wwn_watchdogd_inject(const char *dylib_path);
int wwn_inject_launchd(const char *dylib_path);

#endif /* WWN_IOWATCHDOG_H */
