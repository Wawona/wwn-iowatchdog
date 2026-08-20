/*
 * wwn-iowatchdog: macOS Desktop Mode B only.
 *
 * Owned by github.com/Wawona/wwn-iowatchdog (L3'). Wawona bundles the
 * binary; do not re-vendor this source into the Wawona tree.
 *
 * Disable / re-enable kernel IOWatchdog userspace monitoring so launchctl
 * may unload com.apple.watchdogd without an immediate XNU panic
 * (watchdogd[pid] exited, PanicOnConsecutiveCrash on macOS 26 / 25F80).
 *
 * Never ship on iOS / store IPA. Requires SIP fully disabled + root.
 *
 * Method selectors match /usr/libexec/watchdogd error strings on 25F80.
 *
 * SAFETY (2026-08-20): By default this tool does NOT call IOServiceOpen,
 * lsmp, or attach to watchdogd. Exclusive type=1 opens fail while
 * watchdogd holds the client; the old lldb attach path exited watchdogd
 * with SIGTRAP (paniclog namespace 2 subcode 0x5) and paniced the machine.
 * Set WWN_IOWATCHDOG_ALLOW_OPEN=1 only when experimenting with a proven
 * non-lldb path. Take Over must stay blocked until that exists.
 */
#include <IOKit/IOKitLib.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
  kIOWatchdogDaemonCheckEnabled = 0,
  kIOWatchdogDaemonCheckUserspaceDefanged = 1,
  kIOWatchdogDaemonCheckin = 2,
  kIOWatchdogDaemonDisableUserspaceMonitoring = 3,
  kIOWatchdogDaemonReenableUserspaceMonitoring = 4,
};

typedef struct {
  io_connect_t connection;
} wwn_iow_conn_t;

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s status|disable|enable\n"
          "  Default: refuse IOKit open (safe). Set "
          "WWN_IOWATCHDOG_ALLOW_OPEN=1 to attempt type=1 open.\n",
          argv0);
}

static int allow_open(void) {
  const char *v = getenv("WWN_IOWATCHDOG_ALLOW_OPEN");
  return v != NULL && v[0] == '1' && v[1] == '\0';
}

static void close_conn(wwn_iow_conn_t *c) {
  if (c->connection != IO_OBJECT_NULL) {
    IOServiceClose(c->connection);
    c->connection = IO_OBJECT_NULL;
  }
}

static wwn_iow_conn_t open_watchdog(void) {
  wwn_iow_conn_t out = {.connection = IO_OBJECT_NULL};
  CFMutableDictionaryRef matching = IOServiceMatching("IOWatchdog");
  if (!matching)
    return out;
  io_service_t service =
      IOServiceGetMatchingService(kIOMainPortDefault, matching);
  if (service == IO_OBJECT_NULL)
    return out;
  kern_return_t kr =
      IOServiceOpen(service, mach_task_self(), /*type=*/1, &out.connection);
  IOObjectRelease(service);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog: IOServiceOpen type=1 failed: %s (0x%x)\n",
            mach_error_string(kr), (unsigned)kr);
    out.connection = IO_OBJECT_NULL;
  }
  return out;
}

static int call_scalar(io_connect_t conn, uint32_t selector,
                       const char *label) {
  kern_return_t kr =
      IOConnectCallScalarMethod(conn, selector, NULL, 0, NULL, NULL);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog: %s (selector %u) failed: %s (0x%x)\n",
            label, (unsigned)selector, mach_error_string(kr), (unsigned)kr);
    return 1;
  }
  printf("wwn-iowatchdog: %s ok\n", label);
  return 0;
}

static int refuse_closed(const char *cmd) {
  fprintf(stderr,
          "wwn-iowatchdog: %s refused (default fail-closed). Do not unload "
          "com.apple.watchdogd. IOServiceOpen / lsmp / lldb against "
          "watchdogd are disabled after SIGTRAP panics (2026-08-20). Set "
          "WWN_IOWATCHDOG_ALLOW_OPEN=1 only for a proven exclusive-open "
          "experiment.\n",
          cmd);
  return 1;
}

static int run_selector(uint32_t selector, const char *label) {
  if (!allow_open())
    return refuse_closed(label);
  wwn_iow_conn_t c = open_watchdog();
  if (c.connection != IO_OBJECT_NULL) {
    int rc = call_scalar(c.connection, selector, label);
    close_conn(&c);
    return rc;
  }
  fprintf(stderr,
          "wwn-iowatchdog: %s unavailable (IOWatchdogUserClient exclusive to "
          "watchdogd; no lldb fallback). Do not unload "
          "com.apple.watchdogd.\n",
          label);
  return 1;
}

static int cmd_status(void) {
  if (!allow_open()) {
    printf("wwn-iowatchdog: status (fail-closed; no IOKit open)\n");
    printf("  disable/enable: refused without WWN_IOWATCHDOG_ALLOW_OPEN=1\n");
    printf("  unload com.apple.watchdogd: forbidden\n");
    return 0;
  }
  wwn_iow_conn_t c = open_watchdog();
  if (c.connection != IO_OBJECT_NULL) {
    int a = call_scalar(c.connection, kIOWatchdogDaemonCheckEnabled,
                        "CheckEnabled");
    int b = call_scalar(c.connection, kIOWatchdogDaemonCheckUserspaceDefanged,
                        "CheckUserspaceDefanged");
    close_conn(&c);
    return (a == 0 || b == 0) ? 0 : 1;
  }
  printf("wwn-iowatchdog: status (ALLOW_OPEN set; no connection)\n");
  printf("  disable/enable: blocked without exclusive open\n");
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: must run as root (euid=%d)\n", geteuid());
    return 2;
  }

  const char *cmd = argv[1];
  if (strcmp(cmd, "status") == 0)
    return cmd_status();
  if (strcmp(cmd, "disable") == 0)
    return run_selector(kIOWatchdogDaemonDisableUserspaceMonitoring,
                        "DisableUserspaceMonitoring");
  if (strcmp(cmd, "enable") == 0)
    return run_selector(kIOWatchdogDaemonReenableUserspaceMonitoring,
                        "ReenableUserspaceMonitoring");
  usage(argv[0]);
  return 2;
}
