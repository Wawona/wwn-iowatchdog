/*
 * wwn-iowatchdog: macOS Desktop Mode B only.
 *
 * disable/enable: Path A (entitled direct IOServiceOpen) then Path B
 * (Unix socket to arm64e hook in watchdogd). Live soft-inject stays
 * fail-closed on 25F80. Never lldb. Never kickstart -k watchdogd.
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

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s status|disable|enable|inject|inject-launchd|"
          "claim-install|claim-uninstall\n"
          "  disable/enable: Path A (direct) then Path B (hook sock)\n"
          "  claim-*: opt-in LaunchDaemon that holds exclusive after disable\n"
          "  inject-launchd: only with disable marker present\n",
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

static void write_disabled_marker(const char *src) {
  mkdir("/tmp/libwayland-support", 0755);
  FILE *f = fopen(WWN_IOW_DISABLED_MARKER, "w");
  if (f) {
    fprintf(f, "%s\n", src ? src : "1");
    fclose(f);
  }
}

static int try_disable_enable(uint32_t selector, const char *label,
                              int is_disable) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: must run as root\n");
    return 1;
  }

  /* Path A */
  int a = wwn_direct_scalar(selector);
  if (a == 0) {
    printf("OK %s via Path A (direct)\n", label);
    if (is_disable)
      write_disabled_marker("path-a");
    else
      unlink(WWN_IOW_DISABLED_MARKER);
    return 0;
  }
  if (a == 1)
    fprintf(stderr, "wwn-iowatchdog: Path A exclusive; trying Path B\n");
  else
    fprintf(stderr, "wwn-iowatchdog: Path A failed; trying Path B\n");

  /* Path B */
  if (!wwn_sock_present()) {
    fprintf(stderr,
            "wwn-iowatchdog: Path B sock absent (%s). Install hook via "
            "inject-launchd after a claim disable ACK, or win Path A claim "
            "at boot.\n",
            WWN_IOW_SOCK_PATH);
    return 5;
  }
  char reply[256];
  const char *cmd = is_disable ? "disable" : "enable";
  if (wwn_sock_cmd(cmd, reply, sizeof(reply)) != 0) {
    fprintf(stderr, "wwn-iowatchdog: Path B sock error: %s\n", strerror(errno));
    return 6;
  }
  if (strncmp(reply, "OK", 2) != 0) {
    fprintf(stderr, "wwn-iowatchdog: Path B: %s", reply);
    return 7;
  }
  printf("OK %s via Path B (sock): %s", label, reply);
  if (is_disable)
    write_disabled_marker("path-b");
  else
    unlink(WWN_IOW_DISABLED_MARKER);
  return 0;
}

static int cmd_status(void) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: must run as root\n");
    return 1;
  }
  pid_t pid = find_watchdogd();
  mach_port_name_t port = 0;
  int have_port = 0;
  if (pid > 0 && wwn_find_iowatchdog_port_name(pid, &port) == 0)
    have_port = 1;

  int probe = wwn_direct_open_probe();
  const char *a_state =
      (probe == 0) ? "free" : (probe == 1) ? "exclusive" : "error";
  int sock = wwn_sock_present();
  struct stat st;
  int marker = (stat(WWN_IOW_DISABLED_MARKER, &st) == 0);
  int claim = (stat(WWN_IOW_CLAIM_MARKER, &st) == 0);

  printf("watchdogd=%d IOWatchdogUserClient=%s0x%x pathA=%s sock=%s "
         "marker=%s claim=%s\n",
         (int)pid, have_port ? "" : "none/", have_port ? (unsigned)port : 0u,
         a_state, sock ? "up" : "down", marker ? "yes" : "no",
         claim ? "held" : "no");
  printf("capability: Path A entitled open when free; claim-hold for boot "
         "race; Path B sock after hook load. No seize on 25F80. Take Over "
         "stays blocked until proofs.\n");
  return 0;
}

static const char *claim_plist_path =
    "/Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-claim.plist";

static int claim_install(const char *claim_bin) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: claim-install needs root\n");
    return 1;
  }
  char self[PROC_PIDPATHINFO_MAXSIZE];
  char default_bin[512];
  if (!claim_bin) {
    if (proc_pidpath(getpid(), self, sizeof(self)) <= 0)
      return 2;
    char *slash = strrchr(self, '/');
    if (!slash)
      return 2;
    *slash = '\0';
    snprintf(default_bin, sizeof(default_bin), "%s/wwn-iowatchdog-claim", self);
    claim_bin = default_bin;
  }
  struct stat st;
  if (stat(claim_bin, &st) != 0) {
    fprintf(stderr, "wwn-iowatchdog: claim binary missing: %s\n", claim_bin);
    return 3;
  }
  FILE *f = fopen(claim_plist_path, "w");
  if (!f) {
    fprintf(stderr, "wwn-iowatchdog: cannot write %s: %s\n", claim_plist_path,
            strerror(errno));
    return 4;
  }
  fprintf(f,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "  <key>Label</key>\n"
          "  <string>com.aspauldingcode.wwn-iowatchdog-claim</string>\n"
          "  <key>ProgramArguments</key>\n"
          "  <array>\n"
          "    <string>%s</string>\n"
          "  </array>\n"
          "  <key>RunAtLoad</key>\n"
          "  <true/>\n"
          "  <key>KeepAlive</key>\n"
          "  <true/>\n"
          "  <key>UserName</key>\n"
          "  <string>root</string>\n"
          "  <key>StandardErrorPath</key>\n"
          "  <string>/var/log/wwn-iowatchdog-claim.err.log</string>\n"
          "  <key>StandardOutPath</key>\n"
          "  <string>/var/log/wwn-iowatchdog-claim.out.log</string>\n"
          "</dict>\n"
          "</plist>\n",
          claim_bin);
  fclose(f);
  fprintf(stderr,
          "wwn-iowatchdog: wrote %s\n"
          "  bootstrap: launchctl bootstrap system %s\n"
          "  Reboot so claim can race watchdogd for exclusive open.\n"
          "  Do not kickstart -k watchdogd.\n",
          claim_plist_path, claim_plist_path);
  return 0;
}

static int claim_uninstall(void) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: claim-uninstall needs root\n");
    return 1;
  }
  (void)system("launchctl bootout system/com.aspauldingcode.wwn-iowatchdog-claim "
               "2>/dev/null");
  unlink(claim_plist_path);
  unlink(WWN_IOW_CLAIM_MARKER);
  fprintf(stderr, "wwn-iowatchdog: claim LaunchDaemon removed\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }
  const char *cmd = argv[1];
  if (strcmp(cmd, "status") == 0)
    return cmd_status();
  if (strcmp(cmd, "disable") == 0)
    return try_disable_enable(kIOWatchdogDaemonDisableUserspaceMonitoring,
                              "DisableUserspaceMonitoring", 1);
  if (strcmp(cmd, "enable") == 0)
    return try_disable_enable(kIOWatchdogDaemonReenableUserspaceMonitoring,
                              "ReenableUserspaceMonitoring", 0);
  if (strcmp(cmd, "inject") == 0)
    return wwn_watchdogd_inject(argc >= 3 ? argv[2] : NULL);
  if (strcmp(cmd, "inject-launchd") == 0)
    return wwn_inject_launchd(argc >= 3 ? argv[2] : NULL);
  if (strcmp(cmd, "claim-install") == 0)
    return claim_install(argc >= 3 ? argv[2] : NULL);
  if (strcmp(cmd, "claim-uninstall") == 0)
    return claim_uninstall();
  usage(argv[0]);
  return 2;
}
