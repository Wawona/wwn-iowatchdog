/*
 * Shared constants for wwn-iowatchdog (CLI + hook + inject).
 * Desktop Mode B only. Never ship on Apple mobile.
 */
#ifndef WWN_IOWATCHDOG_H
#define WWN_IOWATCHDOG_H

#define WWN_IOW_SOCK_PATH "/var/run/wwn-iowatchdog.sock"
#define WWN_IOW_HOOK_NAME "libwwn_watchdogd_hook.dylib"
#define WWN_IOW_DISABLED_MARKER \
  "/tmp/libwayland-support/iowatchdog-userspace-disabled"

#endif /* WWN_IOWATCHDOG_H */
