/*
 * Unentitled claim-install / claim-uninstall.
 *
 * Ad-hoc private entitlements on wwn-iowatchdog often cause interactive
 * SIGKILL (137). This helper only writes plists, copies binaries, and
 * talks to launchctl. Sign without com.apple.private.iowatchdog.user-access.
 *
 * usage:
 *   wwn-iowatchdog-claim-install --path-a [pkg]
 *   wwn-iowatchdog-claim-install --path-b [pkg]
 *   wwn-iowatchdog-claim-install --path-a-amfi-nvram
 *   wwn-iowatchdog-claim-install --uninstall
 */
#include "../common/wwn_iowatchdog.h"
#include "../common/wwn_watchdogd_job.h"

#include <errno.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int mkdir_p(const char *path) {
  char cmd[768];
  snprintf(cmd, sizeof(cmd), "/bin/mkdir -p '%s'", path);
  int st = system(cmd);
  if (st == -1)
    return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int copy_file(const char *src, const char *dst) {
  char cmd[1536];
  snprintf(cmd, sizeof(cmd), "/bin/cp -f '%s' '%s' && /bin/chmod 755 '%s'", src,
           dst, dst);
  int st = system(cmd);
  if (st == -1)
    return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int write_claim_plist(const char *claim_bin) {
  FILE *f = fopen(WWN_CLAIM_PLIST, "w");
  if (!f) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: cannot write %s: %s\n",
            WWN_CLAIM_PLIST, strerror(errno));
    return -1;
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
          "  <false/>\n"
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
  return 0;
}

static int write_restore_plist(void) {
  FILE *f = fopen(WWN_RESTORE_PLIST, "w");
  if (!f) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: cannot write %s: %s\n",
            WWN_RESTORE_PLIST, strerror(errno));
    return -1;
  }
  fprintf(f,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "  <key>Label</key>\n"
          "  <string>com.aspauldingcode.wwn-iowatchdog-restore</string>\n"
          "  <key>ProgramArguments</key>\n"
          "  <array>\n"
          "    <string>/bin/bash</string>\n"
          "    <string>-c</string>\n"
          "    <string>"
          "for i in $(seq 1 90); do "
          "  if [ -f /var/db/wwn-iowatchdog/claim-ok ]; then break; fi; "
          "  if [ ! -f /var/db/wwn-iowatchdog/claim-pending ]; then break; fi; "
          "  sleep 1; "
          "done; "
          "/bin/launchctl enable system/com.apple.watchdogd; "
          "/bin/launchctl bootstrap system "
          "/System/Library/LaunchDaemons/com.apple.watchdogd.plist "
          "2>/dev/null; "
          "/bin/launchctl kickstart system/com.apple.watchdogd "
          "2>/dev/null; "
          "rm -f /var/db/wwn-iowatchdog/claim-pending"
          "</string>\n"
          "  </array>\n"
          "  <key>RunAtLoad</key>\n"
          "  <true/>\n"
          "  <key>KeepAlive</key>\n"
          "  <false/>\n"
          "  <key>UserName</key>\n"
          "  <string>root</string>\n"
          "  <key>StandardErrorPath</key>\n"
          "  <string>/var/log/wwn-iowatchdog-restore.err.log</string>\n"
          "  <key>StandardOutPath</key>\n"
          "  <string>/var/log/wwn-iowatchdog-restore.out.log</string>\n"
          "</dict>\n"
          "</plist>\n");
  fclose(f);
  return 0;
}

static int launchctl_bootout(const char *label) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "/bin/launchctl bootout system/%s 2>/dev/null", label);
  (void)system(cmd);
  return 0;
}

static int launchctl_bootstrap(const char *plist) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "/bin/launchctl bootstrap system '%s'", plist);
  int st = system(cmd);
  if (st == -1)
    return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Resolve claim + CLI sources from optional path or dirname(self). */
static int resolve_sources(const char *arg, char *claim_src, size_t claim_sz,
                           char *cli_src, size_t cli_sz) {
  char self[PROC_PIDPATHINFO_MAXSIZE];
  char dir[PROC_PIDPATHINFO_MAXSIZE];
  struct stat st;

  if (arg && arg[0]) {
    if (stat(arg, &st) != 0) {
      fprintf(stderr, "wwn-iowatchdog-claim-install: missing: %s\n", arg);
      return -1;
    }
    if (S_ISDIR(st.st_mode)) {
      /* Accept …/bin or package root (…/bin siblings). */
      snprintf(claim_src, claim_sz, "%s/wwn-iowatchdog-claim", arg);
      snprintf(cli_src, cli_sz, "%s/wwn-iowatchdog", arg);
      if (stat(claim_src, &st) != 0) {
        snprintf(claim_src, claim_sz, "%s/bin/wwn-iowatchdog-claim", arg);
        snprintf(cli_src, cli_sz, "%s/bin/wwn-iowatchdog", arg);
      }
    } else {
      /* Path to claim binary; CLI is sibling. */
      snprintf(claim_src, claim_sz, "%s", arg);
      snprintf(dir, sizeof(dir), "%s", arg);
      char *slash = strrchr(dir, '/');
      if (!slash)
        return -1;
      *slash = '\0';
      snprintf(cli_src, cli_sz, "%s/wwn-iowatchdog", dir);
    }
  } else {
    if (proc_pidpath(getpid(), self, sizeof(self)) <= 0)
      return -1;
    snprintf(dir, sizeof(dir), "%s", self);
    char *slash = strrchr(dir, '/');
    if (!slash)
      return -1;
    *slash = '\0';
    snprintf(claim_src, claim_sz, "%s/wwn-iowatchdog-claim", dir);
    snprintf(cli_src, cli_sz, "%s/wwn-iowatchdog", dir);
  }

  if (stat(claim_src, &st) != 0 || !S_ISREG(st.st_mode)) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: claim binary missing: %s\n",
            claim_src);
    return -1;
  }
  return 0;
}

static int bootargs_has_amfi_relaxed(void) {
  char buf[1024];
  size_t len = sizeof(buf);
  if (sysctlbyname("kern.bootargs", buf, &len, NULL, 0) != 0)
    return 0;
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  buf[len] = '\0';
  return strstr(buf, "amfi_get_out_of_my_way=1") != NULL;
}

static int do_patha_amfi_nvram(void) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: --path-a-amfi-nvram needs root\n");
    return 1;
  }
  if (bootargs_has_amfi_relaxed()) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: amfi_get_out_of_my_way=1 already set\n");
    return 0;
  }
  char cur[1024];
  size_t len = sizeof(cur);
  if (sysctlbyname("kern.bootargs", cur, &len, NULL, 0) != 0)
    cur[0] = '\0';
  else {
    if (len >= sizeof(cur))
      len = sizeof(cur) - 1;
    cur[len] = '\0';
  }
  char cmd[1536];
  if (cur[0])
    snprintf(cmd, sizeof(cmd),
             "/usr/sbin/nvram 'boot-args=%s amfi_get_out_of_my_way=1'", cur);
  else
    snprintf(cmd, sizeof(cmd),
             "/usr/sbin/nvram 'boot-args=amfi_get_out_of_my_way=1'");
  int st = system(cmd);
  if (st == -1 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: nvram failed\n");
    return 2;
  }
  fprintf(stderr,
          "wwn-iowatchdog-claim-install: nvram updated. Reboot, then:\n"
          "  sudo wwn-iowatchdog-claim-install --path-a /path/to/pkg\n"
          "Revert: sudo nvram 'boot-args=%s'\n",
          cur[0] ? cur : "");
  return 0;
}

static int do_install(const char *arg) {
  char claim_src[512];
  char cli_src[512];

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: needs root\n");
    return 1;
  }
  /* 25F80: adhoc forged entitlement → OS_REASON_CODESIGNING without AMFI off. */
  if (getenv("WWN_IOW_PATHA_EXPERIMENT") == NULL) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: Path A refused without opt-in.\n"
            "  Use: sudo wwn-iowatchdog-claim-install --path-a [pkg]\n"
            "  Or:  WWN_IOW_PATHA_EXPERIMENT=1 sudo … [pkg]\n"
            "  Needs amfi_get_out_of_my_way=1 (see --path-a-amfi-nvram).\n"
            "  Prefer Path B (proven sticky): --path-b\n");
    return 20;
  }
  if (!bootargs_has_amfi_relaxed() &&
      getenv("WWN_IOW_PATHA_FORCE") == NULL) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: Path A needs "
            "amfi_get_out_of_my_way=1 in kern.bootargs.\n"
            "  sudo wwn-iowatchdog-claim-install --path-a-amfi-nvram && reboot\n"
            "  Or force (expect 137): WWN_IOW_PATHA_FORCE=1 … --path-a\n");
    return 21;
  }
  if (resolve_sources(arg, claim_src, sizeof(claim_src), cli_src,
                      sizeof(cli_src)) != 0)
    return 2;

  if (mkdir_p(WWN_IOW_INSTALL_DIR) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: mkdir %s failed\n",
            WWN_IOW_INSTALL_DIR);
    return 3;
  }
  if (copy_file(claim_src, WWN_IOW_INSTALLED_CLAIM) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: copy claim failed\n");
    return 4;
  }
  {
    struct stat cli_st;
    if (stat(cli_src, &cli_st) == 0 && S_ISREG(cli_st.st_mode)) {
      if (copy_file(cli_src, WWN_IOW_INSTALLED_CLI) != 0)
        fprintf(stderr,
                "wwn-iowatchdog-claim-install: WARNING: copy CLI failed\n");
    }
  }

  if (wwn_iow_db_mkdir() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: mkdir %s: %s\n",
            WWN_IOW_DB_DIR, strerror(errno));
    return 5;
  }
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_OK_STAMP);
  if (wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "path-a-armed\n") != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: cannot write claim-pending\n");
    return 6;
  }

  if (write_claim_plist(WWN_IOW_INSTALLED_CLAIM) != 0)
    return 7;
  if (write_restore_plist() != 0)
    return 8;

  /* Path A and Path B are mutually exclusive holders. */
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");
  unlink(WWN_PATHB_PLIST);

  if (wwn_watchdogd_job_disable() != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: WARNING: launchctl disable "
            "system/com.apple.watchdogd failed\n");
  }

  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");
  /*
   * Stage plists only (like Path B). Bootstrapping claim+restore in this
   * session races the restore helper (90s then re-enables Apple and clears
   * pending). Claim RunAtLoad on the next reboot wins exclusive.
   */
  fprintf(stderr,
          "wwn-iowatchdog-claim-install: Path A claim armed.\n"
          "  Claim: %s (entitled iowatchdog.user-access)\n"
          "  Plists staged; Apple watchdogd persist-disabled.\n"
          "  Reboot now. After login:\n"
          "    cat /var/db/wwn-iowatchdog/claim-ok\n"
          "    cat /tmp/libwayland-support/iowatchdog-userspace-disabled\n"
          "  Abort: sudo wwn-iowatchdog-claim-install --uninstall\n",
          WWN_IOW_INSTALLED_CLAIM);
  return 0;
}

/* Resolve package root (…/bin parent) for lib/ hook. */
static int resolve_pkg_root(const char *arg, char *root, size_t root_sz) {
  char self[PROC_PIDPATHINFO_MAXSIZE];
  char dir[PROC_PIDPATHINFO_MAXSIZE];
  struct stat st;

  if (arg && arg[0] && stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
    /* If arg is …/bin, parent is root; if arg is root, use as-is. */
    snprintf(dir, sizeof(dir), "%s/lib/%s", arg, WWN_IOW_HOOK_NAME);
    if (stat(dir, &st) == 0) {
      snprintf(root, root_sz, "%s", arg);
      return 0;
    }
    snprintf(dir, sizeof(dir), "%s/../lib/%s", arg, WWN_IOW_HOOK_NAME);
    /* arg might be bin/ */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s/..", arg);
    snprintf(dir, sizeof(dir), "%s/lib/%s", parent, WWN_IOW_HOOK_NAME);
    /* realpath-ish: if arg ends with /bin */
    size_t n = strlen(arg);
    if (n >= 4 && strcmp(arg + n - 4, "/bin") == 0) {
      snprintf(root, root_sz, "%.*s", (int)(n - 4), arg);
      return 0;
    }
    snprintf(root, root_sz, "%s", arg);
    return 0;
  }

  if (proc_pidpath(getpid(), self, sizeof(self)) <= 0)
    return -1;
  snprintf(dir, sizeof(dir), "%s", self);
  char *slash = strrchr(dir, '/');
  if (!slash)
    return -1;
  *slash = '\0'; /* …/bin */
  slash = strrchr(dir, '/');
  if (!slash)
    return -1;
  *slash = '\0'; /* package root */
  snprintf(root, root_sz, "%s", dir);
  return 0;
}

static int write_pathb_wrapper(void) {
  FILE *f = fopen(WWN_IOW_INSTALLED_WRAPPER, "w");
  if (!f)
    return -1;
  fprintf(f,
          "#!/bin/bash\n"
          "# Path B: load arm64e hook into Apple's watchdogd (no forged "
          "entitlements).\n"
          "export DYLD_INSERT_LIBRARIES='%s'\n"
          "export WWN_IOW_AUTO_DISABLE=1\n"
          "exec /usr/libexec/watchdogd \"$@\"\n",
          WWN_IOW_INSTALLED_HOOK);
  fclose(f);
  chmod(WWN_IOW_INSTALLED_WRAPPER, 0755);
  return 0;
}

static int write_pathb_plist(void) {
  FILE *f = fopen(WWN_PATHB_PLIST, "w");
  if (!f) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: cannot write %s: %s\n",
            WWN_PATHB_PLIST, strerror(errno));
    return -1;
  }
  fprintf(f,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "  <key>Label</key>\n"
          "  <string>com.aspauldingcode.wwn-iowatchdog-pathb</string>\n"
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
          "  <string>/var/log/wwn-iowatchdog-pathb.err.log</string>\n"
          "  <key>StandardOutPath</key>\n"
          "  <string>/var/log/wwn-iowatchdog-pathb.out.log</string>\n"
          "</dict>\n"
          "</plist>\n",
          WWN_IOW_INSTALLED_WRAPPER);
  fclose(f);
  return 0;
}

static int do_install_pathb(const char *arg) {
  char root[512];
  char hook_src[576];
  struct stat st;

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: --path-b needs root\n");
    return 1;
  }
  /* 25F80: fishhook SIGBUS fixed via DYLD_INTERPOSE (0.3.6). Path B OK. */
  if (resolve_pkg_root(arg, root, sizeof(root)) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: cannot resolve package root\n");
    return 2;
  }
  snprintf(hook_src, sizeof(hook_src), "%s/lib/%s", root, WWN_IOW_HOOK_NAME);
  if (stat(hook_src, &st) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: hook missing: %s\n",
            hook_src);
    return 3;
  }

  if (mkdir_p(WWN_IOW_INSTALL_DIR) != 0)
    return 4;
  if (copy_file(hook_src, WWN_IOW_INSTALLED_HOOK) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: copy hook failed\n");
    return 5;
  }
  if (write_pathb_wrapper() != 0)
    return 6;
  if (write_pathb_plist() != 0)
    return 7;

  if (wwn_iow_db_mkdir() != 0)
    return 8;
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_OK_STAMP);
  if (wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "path-b-armed\n") != 0)
    return 9;

  /* Tear down Path A claim daemons (adhoc entitled claim dies on AMFI). */
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);

  if (wwn_watchdogd_job_disable() != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: WARNING: disable "
            "com.apple.watchdogd failed\n");
  }

  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");
  /*
   * Do not bootstrap pathb while Apple's watchdogd is still live this
   * session (KeepAlive thrash / exclusive conflict). Plist in
   * /Library/LaunchDaemons loads on the next reboot after Apple's job
   * stays persist-disabled.
   */
  fprintf(stderr,
          "wwn-iowatchdog-claim-install: Path B sticky armed.\n"
          "  Hook: %s\n"
          "  Plist staged (loads next boot; not started this session).\n"
          "  Auto-disable after Checkin (WWN_IOW_AUTO_DISABLE=1).\n"
          "  Reboot now. After login:\n"
          "    cat /var/db/wwn-iowatchdog/claim-ok\n"
          "    cat /tmp/libwayland-support/iowatchdog-userspace-disabled\n"
          "    ls -la /var/run/wwn-iowatchdog.sock\n"
          "  Abort without reboot:\n"
          "    sudo wwn-iowatchdog-claim-install --uninstall\n",
          WWN_IOW_INSTALLED_HOOK);
  return 0;
}

static int do_uninstall(void) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: --uninstall needs root\n");
    return 1;
  }
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);
  unlink(WWN_PATHB_PLIST);
  unlink(WWN_IOW_CLAIM_MARKER);
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
  /* Keep durable binaries; only disarm jobs. */
  if (wwn_watchdogd_job_restore() != 0)
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: WARNING: failed to restore "
            "com.apple.watchdogd\n");
  fprintf(stderr,
          "wwn-iowatchdog-claim-install: claim/pathb/restore removed; "
          "com.apple.watchdogd re-enabled\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                    strcmp(argv[1], "--help") == 0)) {
    fprintf(stderr,
            "usage: wwn-iowatchdog-claim-install --path-a [pkg]\n"
            "       wwn-iowatchdog-claim-install --path-b [pkg]\n"
            "       wwn-iowatchdog-claim-install --path-a-amfi-nvram\n"
            "       wwn-iowatchdog-claim-install --uninstall\n"
            "  --path-b: DYLD_INTERPOSE hook (0.3.7+; reboot sticky proven)\n"
            "  --path-a: entitled claim daemon (needs amfi_get_out_of_my_way=1)\n"
            "  --path-a-amfi-nvram: append that boot-arg (reboot after)\n"
            "  pkg: nix result root or …/bin (default: next to this binary)\n");
    return 0;
  }
  if (argc >= 2 && strcmp(argv[1], "--uninstall") == 0)
    return do_uninstall();
  if (argc >= 2 && strcmp(argv[1], "--path-a-amfi-nvram") == 0)
    return do_patha_amfi_nvram();
  if (argc >= 2 && strcmp(argv[1], "--path-b") == 0)
    return do_install_pathb(argc >= 3 ? argv[2] : NULL);
  if (argc >= 2 && strcmp(argv[1], "--path-a") == 0) {
    setenv("WWN_IOW_PATHA_EXPERIMENT", "1", 1);
    return do_install(argc >= 3 ? argv[2] : NULL);
  }
  return do_install(argc >= 2 ? argv[1] : NULL);
}
