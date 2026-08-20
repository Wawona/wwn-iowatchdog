/*
 * wwn-iowatchdog: macOS Desktop Mode B only.
 *
 * disable/enable: locate IOWatchdogUserClient port name inside live
 * watchdogd (mach_port_kobject_description), then thread-hijack a worker
 * to run IOConnectCallScalarMethod in-process. Never lldb. Never new
 * IOServiceOpen type=1. Never kickstart -k watchdogd.
 *
 * Owned by github.com/Wawona/wwn-iowatchdog (L3'). Never ship on Apple mobile.
 */
#include "common/wwn_iowatchdog.h"

#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
  kIOWatchdogDaemonCheckEnabled = 0,
  kIOWatchdogDaemonCheckUserspaceDefanged = 1,
  kIOWatchdogDaemonDisableUserspaceMonitoring = 3,
  kIOWatchdogDaemonReenableUserspaceMonitoring = 4,
};

int wwn_find_iowatchdog_port_name(pid_t pid, mach_port_name_t *out_name);
int wwn_remote_ioconnect_scalar(pid_t pid, mach_port_name_t port_name,
                                uint32_t selector);
int wwn_watchdogd_inject(const char *dylib_path);

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s status|disable|enable|inject\n"
          "  status/disable/enable: remote IOConnect via watchdogd (root)\n",
          argv0);
}

static pid_t find_watchdogd(void) {
  int n = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0) / (int)sizeof(pid_t);
  pid_t *pids = calloc((size_t)n, sizeof(pid_t));
  if (!pids)
    return -1;
  n = proc_listpids(PROC_ALL_PIDS, 0, pids, n * (int)sizeof(pid_t)) /
      (int)sizeof(pid_t);
  pid_t found = -1;
  for (int i = 0; i < n; i++) {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (pids[i] <= 0)
      continue;
    if (proc_pidpath(pids[i], path, sizeof(path)) <= 0)
      continue;
    if (strcmp(path, "/usr/libexec/watchdogd") == 0) {
      found = pids[i];
      break;
    }
  }
  free(pids);
  return found;
}

static int with_remote(uint32_t selector, const char *label) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: must run as root\n");
    return 1;
  }
  pid_t pid = find_watchdogd();
  if (pid <= 0) {
    fprintf(stderr, "wwn-iowatchdog: watchdogd not found\n");
    return 2;
  }
  mach_port_name_t port = 0;
  if (wwn_find_iowatchdog_port_name(pid, &port) != 0) {
    fprintf(stderr, "wwn-iowatchdog: no IOWatchdogUserClient in watchdogd\n");
    return 3;
  }
  fprintf(stderr, "wwn-iowatchdog: %s via port 0x%x in pid %d\n", label,
          (unsigned)port, (int)pid);
  int rc = wwn_remote_ioconnect_scalar(pid, port, selector);
  if (rc == -2)
    return 6;
  if (rc != 0)
    return 4;
  printf("OK %s\n", label);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }
  const char *cmd = argv[1];
  if (strcmp(cmd, "status") == 0) {
    if (geteuid() != 0) {
      fprintf(stderr, "wwn-iowatchdog: must run as root\n");
      return 1;
    }
    pid_t pid = find_watchdogd();
    if (pid <= 0) {
      fprintf(stderr, "wwn-iowatchdog: watchdogd not found\n");
      return 2;
    }
    mach_port_name_t port = 0;
    if (wwn_find_iowatchdog_port_name(pid, &port) != 0) {
      fprintf(stderr, "wwn-iowatchdog: no IOWatchdogUserClient\n");
      return 3;
    }
    printf("OK watchdogd=%d IOWatchdogUserClient=0x%x "
           "(use disable/enable for remote call)\n",
           (int)pid, (unsigned)port);
    return 0;
  }
  if (strcmp(cmd, "disable") == 0) {
    int rc = with_remote(kIOWatchdogDaemonDisableUserspaceMonitoring,
                         "DisableUserspaceMonitoring");
    if (rc == 0) {
      mkdir("/tmp/libwayland-support", 0755);
      FILE *f = fopen(WWN_IOW_DISABLED_MARKER, "w");
      if (f) {
        fputs("1\n", f);
        fclose(f);
      }
    }
    return rc;
  }
  if (strcmp(cmd, "enable") == 0) {
    int rc = with_remote(kIOWatchdogDaemonReenableUserspaceMonitoring,
                         "ReenableUserspaceMonitoring");
    if (rc == 0)
      unlink(WWN_IOW_DISABLED_MARKER);
    return rc;
  }
  if (strcmp(cmd, "inject") == 0)
    return wwn_watchdogd_inject(argc >= 3 ? argv[2] : NULL);
  usage(argv[0]);
  return 2;
}
