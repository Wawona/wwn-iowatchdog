/*
 * Unentitled claim-install / claim-uninstall / doctor / heal.
 *
 * Ad-hoc private entitlements on wwn-iowatchdog often cause interactive
 * SIGKILL (137). This helper only writes plists, copies binaries, and
 * talks to launchctl. Sign without com.apple.private.iowatchdog.user-access.
 *
 * usage:
 *   wwn-iowatchdog-claim-install --path-a [pkg]
 *   wwn-iowatchdog-claim-install --path-b [pkg]
 *   wwn-iowatchdog-claim-install --path-a-amfi-nvram
 *   wwn-iowatchdog-claim-install --uninstall|--heal
 *   wwn-iowatchdog-claim-install --doctor|--verify
 */
#include "../common/wwn_iowatchdog.h"
#include "../common/wwn_safety.h"
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
          "if ! /usr/bin/pgrep -qx watchdogd; then "
          "  /bin/launchctl bootout system/com.apple.watchdogd 2>/dev/null; "
          "  /bin/launchctl bootstrap system "
          "/System/Library/LaunchDaemons/com.apple.watchdogd.plist "
          "2>/dev/null; "
          "fi; "
          "alive=0; "
          "for j in $(seq 1 40); do "
          "  if /usr/bin/pgrep -qx watchdogd; then "
          "    sleep 1.5; "
          "    if /usr/bin/pgrep -qx watchdogd; then alive=1; break; fi; "
          "  else "
          "    /bin/launchctl kickstart system/com.apple.watchdogd 2>/dev/null; "
          "  fi; "
          "  sleep 0.2; "
          "done; "
          "if [ \"$alive\" != 1 ]; then "
          "  echo 'wwn-restore: no live watchdogd after bootstrap' >&2; "
          "  echo coverage-fail > /var/db/wwn-iowatchdog/coverage-fail; "
          "  exit 1; "
          "fi; "
          "rm -f /var/db/wwn-iowatchdog/claim-pending "
          "/var/db/wwn-iowatchdog/coverage-fail"
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

static void abort_path_a_stage(void) {
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
}

static void abort_path_b_stage(void) {
  unlink(WWN_PATHB_PLIST);
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
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
      snprintf(claim_src, claim_sz, "%s/wwn-iowatchdog-claim", arg);
      snprintf(cli_src, cli_sz, "%s/wwn-iowatchdog", arg);
      if (stat(claim_src, &st) != 0) {
        snprintf(claim_src, claim_sz, "%s/bin/wwn-iowatchdog-claim", arg);
        snprintf(cli_src, cli_sz, "%s/bin/wwn-iowatchdog", arg);
      }
    } else {
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
  return wwn_safety_amfi_relaxed();
}

/* Escape single quotes for nvram 'boot-args=…' shell string. */
static void shell_single_quote_escape(const char *in, char *out, size_t out_sz) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 4 < out_sz; i++) {
    if (in[i] == '\'') {
      /* end quote, literal ', reopen: '"'"' */
      if (j + 4 >= out_sz)
        break;
      out[j++] = '\'';
      out[j++] = '"';
      out[j++] = '\'';
      out[j++] = '"';
      out[j++] = '\'';
    } else {
      out[j++] = in[i];
    }
  }
  out[j] = '\0';
}

static int do_patha_amfi_nvram(void) {
  if (geteuid() != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: --path-a-amfi-nvram needs root\n");
    return 1;
  }
  if (bootargs_has_amfi_relaxed()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: amfi_get_out_of_my_way=1 already "
            "set\n");
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
  char esc[2048];
  char cmd[4096];
  if (cur[0]) {
    shell_single_quote_escape(cur, esc, sizeof(esc));
    snprintf(cmd, sizeof(cmd),
             "/usr/sbin/nvram 'boot-args=%s amfi_get_out_of_my_way=1'", esc);
  } else {
    snprintf(cmd, sizeof(cmd),
             "/usr/sbin/nvram 'boot-args=amfi_get_out_of_my_way=1'");
  }
  int st = system(cmd);
  if (st == -1 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: nvram failed\n");
    return 2;
  }
  fprintf(stderr,
          "wwn-iowatchdog-claim-install: nvram updated. Reboot, then:\n"
          "  sudo wwn-iowatchdog-claim-install --path-a /path/to/pkg\n"
          "Revert: sudo nvram boot-args=… (restore prior args)\n");
  return 0;
}

static int do_install(const char *arg) {
  char claim_src[512];
  char cli_src[512];
  int lock = -1;
  int rc = 0;

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: needs root\n");
    return 1;
  }
  if (getenv("WWN_IOW_PATHA_EXPERIMENT") == NULL) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: Path A refused without opt-in.\n"
            "  Use: sudo wwn-iowatchdog-claim-install --path-a [pkg]\n"
            "  Or:  WWN_IOW_PATHA_EXPERIMENT=1 sudo … [pkg]\n"
            "  Needs amfi_get_out_of_my_way=1 (see --path-a-amfi-nvram).\n"
            "  Prefer Path B (proven sticky): --path-b\n");
    return 20;
  }

  lock = wwn_safety_arm_lock();
  if (lock < 0)
    return 19;

  /* Mutually exclusive: tear down Path B before Path A preflight. */
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");
  unlink(WWN_PATHB_PLIST);

  {
    int pf = wwn_safety_preflight_arm(WWN_SAFETY_PATH_A, NULL);
    if (pf != 0) {
      rc = 20 + pf;
      goto out;
    }
  }

  if (resolve_sources(arg, claim_src, sizeof(claim_src), cli_src,
                      sizeof(cli_src)) != 0) {
    rc = 2;
    goto out;
  }

  if (mkdir_p(WWN_IOW_INSTALL_DIR) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: mkdir %s failed\n",
            WWN_IOW_INSTALL_DIR);
    rc = 3;
    goto out;
  }
  if (copy_file(claim_src, WWN_IOW_INSTALLED_CLAIM) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: copy claim failed\n");
    rc = 4;
    goto out;
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
    rc = 5;
    goto out;
  }
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_OK_STAMP);
  if (wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "path-a-armed\n") != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: cannot write claim-pending\n");
    rc = 6;
    goto out;
  }

  if (write_claim_plist(WWN_IOW_INSTALLED_CLAIM) != 0) {
    abort_path_a_stage();
    rc = 7;
    goto out;
  }
  if (write_restore_plist() != 0) {
    abort_path_a_stage();
    rc = 8;
    goto out;
  }

  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");

  if (!wwn_watchdogd_process_alive()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: no live watchdogd before "
            "persist-disable; aborting arm\n");
    abort_path_a_stage();
    (void)wwn_safety_postflight("path-a-arm-abort");
    rc = 22;
    goto out;
  }

  if (wwn_watchdogd_job_disable_verified() != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: persist-disable failed/unverified; "
            "aborting\n");
    abort_path_a_stage();
    (void)wwn_safety_postflight("path-a-disable-fail");
    rc = 23;
    goto out;
  }

  if (wwn_safety_postflight("path-a-arm") != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: postflight uncovered; disarming\n");
    abort_path_a_stage();
    (void)wwn_watchdogd_job_restore();
    rc = 24;
    goto out;
  }

  if (!wwn_safety_reboot_successor_ok()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: successor check failed after arm\n");
    abort_path_a_stage();
    (void)wwn_watchdogd_job_restore();
    (void)wwn_safety_postflight("path-a-successor-fail");
    rc = 25;
    goto out;
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim-install: Path A claim armed.\n"
          "  Claim: %s (entitled iowatchdog.user-access)\n"
          "  Plists staged; Apple job persist-disabled; process still live.\n"
          "  Reboot now. After login:\n"
          "    sudo wwn-iowatchdog-claim-install --doctor\n"
          "    cat /var/db/wwn-iowatchdog/claim-ok\n"
          "  Abort: sudo wwn-iowatchdog-claim-install --uninstall\n",
          WWN_IOW_INSTALLED_CLAIM);
  rc = 0;

out:
  wwn_safety_arm_unlock(lock);
  return rc;
}

static int resolve_pkg_root(const char *arg, char *root, size_t root_sz) {
  char self[PROC_PIDPATHINFO_MAXSIZE];
  char dir[PROC_PIDPATHINFO_MAXSIZE];
  struct stat st;

  if (arg && arg[0] && stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) {
    snprintf(dir, sizeof(dir), "%s/lib/%s", arg, WWN_IOW_HOOK_NAME);
    if (stat(dir, &st) == 0) {
      snprintf(root, root_sz, "%s", arg);
      return 0;
    }
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
  *slash = '\0';
  slash = strrchr(dir, '/');
  if (!slash)
    return -1;
  *slash = '\0';
  snprintf(root, root_sz, "%s", dir);
  return 0;
}

static int write_pathb_wrapper(void) {
  FILE *f = fopen(WWN_IOW_INSTALLED_WRAPPER, "w");
  if (!f)
    return -1;
  fprintf(f,
          "#!/bin/bash\n"
          "# Path B: prefix env on exec only (never export DYLD_INSERT in "
          "the parent shell).\n"
          "exec /usr/bin/env "
          "DYLD_INSERT_LIBRARIES='%s' "
          "WWN_IOW_AUTO_DISABLE=1 "
          "/usr/libexec/watchdogd \"$@\"\n",
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
  int lock = -1;
  int rc = 0;

  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: --path-b needs root\n");
    return 1;
  }
  lock = wwn_safety_arm_lock();
  if (lock < 0)
    return 19;

  if (resolve_pkg_root(arg, root, sizeof(root)) != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: cannot resolve package root\n");
    rc = 2;
    goto out;
  }
  snprintf(hook_src, sizeof(hook_src), "%s/lib/%s", root, WWN_IOW_HOOK_NAME);
  if (stat(hook_src, &st) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: hook missing: %s\n",
            hook_src);
    rc = 3;
    goto out;
  }

  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);

  {
    int pf = wwn_safety_preflight_arm(WWN_SAFETY_PATH_B, hook_src);
    if (pf != 0) {
      rc = 30 + pf;
      goto out;
    }
  }

  if (mkdir_p(WWN_IOW_INSTALL_DIR) != 0) {
    rc = 4;
    goto out;
  }
  if (copy_file(hook_src, WWN_IOW_INSTALLED_HOOK) != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: copy hook failed\n");
    rc = 5;
    goto out;
  }
  if (!wwn_safety_hook_is_arm64e(WWN_IOW_INSTALLED_HOOK) ||
      !wwn_safety_hook_codesign_ok(WWN_IOW_INSTALLED_HOOK)) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: installed hook failed "
            "arm64e/codesign check\n");
    unlink(WWN_IOW_INSTALLED_HOOK);
    rc = 35;
    goto out;
  }
  if (write_pathb_wrapper() != 0) {
    rc = 6;
    goto out;
  }
  if (write_pathb_plist() != 0) {
    abort_path_b_stage();
    rc = 7;
    goto out;
  }

  if (wwn_iow_db_mkdir() != 0) {
    abort_path_b_stage();
    rc = 8;
    goto out;
  }
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_OK_STAMP);
  if (wwn_iow_write_file(WWN_IOW_CLAIM_PENDING, "path-b-armed\n") != 0) {
    abort_path_b_stage();
    rc = 9;
    goto out;
  }

  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");

  if (!wwn_watchdogd_process_alive()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: no live watchdogd before "
            "persist-disable; aborting Path B arm\n");
    abort_path_b_stage();
    (void)wwn_safety_postflight("path-b-arm-abort");
    rc = 32;
    goto out;
  }

  if (wwn_watchdogd_job_disable_verified() != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: persist-disable failed/unverified; "
            "aborting\n");
    abort_path_b_stage();
    (void)wwn_safety_postflight("path-b-disable-fail");
    rc = 33;
    goto out;
  }

  if (wwn_safety_postflight("path-b-arm") != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: postflight uncovered; disarming "
            "Path B\n");
    abort_path_b_stage();
    (void)wwn_watchdogd_job_restore();
    rc = 34;
    goto out;
  }

  if (!wwn_safety_reboot_successor_ok()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: successor check failed after Path B "
            "arm\n");
    abort_path_b_stage();
    (void)wwn_watchdogd_job_restore();
    (void)wwn_safety_postflight("path-b-successor-fail");
    rc = 36;
    goto out;
  }

  fprintf(stderr,
          "wwn-iowatchdog-claim-install: Path B sticky armed.\n"
          "  Hook: %s\n"
          "  Plist staged (loads next boot); Apple process still live.\n"
          "  Auto-disable after Checkin (WWN_IOW_AUTO_DISABLE=1).\n"
          "  Reboot now. After login:\n"
          "    sudo wwn-iowatchdog-claim-install --doctor\n"
          "    cat /var/db/wwn-iowatchdog/claim-ok\n"
          "  Abort: sudo wwn-iowatchdog-claim-install --uninstall\n",
          WWN_IOW_INSTALLED_HOOK);
  rc = 0;

out:
  wwn_safety_arm_unlock(lock);
  return rc;
}

static int do_uninstall(void) {
  int lock = -1;
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog-claim-install: --uninstall needs root\n");
    return 1;
  }
  lock = wwn_safety_arm_lock();
  if (lock < 0)
    return 19;

  /*
   * Order matters: enable Apple first, then bootout our holders (Path B may
   * be the live watchdogd). Wait until that binary is gone before starting
   * Apple (a second /usr/libexec/watchdogd is SIGTRAP class). Restore
   * re-registers IOKit LaunchEvents; kickstart without -k is last resort.
   */
  (void)wwn_watchdogd_job_enable();
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-claim");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-restore");
  launchctl_bootout("com.aspauldingcode.wwn-iowatchdog-pathb");
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);
  unlink(WWN_PATHB_PLIST);
  unlink(WWN_IOW_CLAIM_MARKER);
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);

  int rc = 0;
  if (wwn_watchdogd_wait_gone(5000) != 0) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: HARD FAIL: watchdogd still alive "
            "after Path A/B bootout; not starting a second Apple daemon\n");
    rc = 2;
  } else if (wwn_watchdogd_job_restore() != 0 ||
      wwn_safety_postflight("uninstall") != 0 ||
      !wwn_safety_reboot_successor_ok()) {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: HARD FAIL: uninstall left machine "
            "uncovered or Apple still persist-disabled\n"
            "  Try: sudo wwn-iowatchdog-claim-install --heal\n");
    rc = 2;
  } else {
    fprintf(stderr,
            "wwn-iowatchdog-claim-install: claim/pathb/restore removed; "
            "com.apple.watchdogd live again\n");
  }
  wwn_safety_arm_unlock(lock);
  return rc;
}

int main(int argc, char **argv) {
  if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                    strcmp(argv[1], "--help") == 0)) {
    fprintf(stderr,
            "usage: wwn-iowatchdog-claim-install --path-a [pkg]\n"
            "       wwn-iowatchdog-claim-install --path-b [pkg]\n"
            "       wwn-iowatchdog-claim-install --path-a-amfi-nvram\n"
            "       wwn-iowatchdog-claim-install --uninstall|--heal\n"
            "       wwn-iowatchdog-claim-install --doctor|--verify\n"
            "  --path-b: DYLD_INTERPOSE hook (reboot sticky proven)\n"
            "  --path-a: entitled claim (needs amfi_get_out_of_my_way=1)\n"
            "  --path-a-amfi-nvram: append that boot-arg (reboot after)\n"
            "  --doctor/--verify: coverage + reboot-successor (exit 0 iff OK)\n"
            "  --heal: tear down Path A/B; restore Apple coverage\n"
            "  --uninstall: same as heal for jobs/plists (keeps claim-ok)\n"
            "  pkg: nix result root or …/bin (default: next to this binary)\n");
    return 0;
  }
  if (argc >= 2 && (strcmp(argv[1], "--doctor") == 0 ||
                    strcmp(argv[1], "--verify") == 0))
    return wwn_safety_doctor();
  if (argc >= 2 && strcmp(argv[1], "--heal") == 0)
    return wwn_safety_heal();
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
