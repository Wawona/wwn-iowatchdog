#include "wwn_safety.h"
#include "wwn_watchdogd_job.h"

#include <errno.h>
#include <fcntl.h>
#include <libkern/OSByteOrder.h>
#include <libproc.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef CPU_SUBTYPE_ARM64E
#define CPU_SUBTYPE_ARM64E ((cpu_subtype_t)2)
#endif

pid_t wwn_watchdogd_find_pid(void) {
  int n = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0) / (int)sizeof(pid_t);
  if (n <= 0)
    return -1;
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

int wwn_watchdogd_process_alive(void) {
  return wwn_watchdogd_find_pid() > 0 ? 1 : 0;
}

int wwn_coverage_ok(void) { return wwn_watchdogd_process_alive(); }

int wwn_safety_amfi_relaxed(void) {
  char buf[1024];
  size_t len = sizeof(buf);
  if (sysctlbyname("kern.bootargs", buf, &len, NULL, 0) != 0)
    return 0;
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  buf[len] = '\0';
  return strstr(buf, "amfi_get_out_of_my_way=1") != NULL;
}

int wwn_safety_path_a_armed(void) {
  struct stat st;
  return (stat(WWN_CLAIM_PLIST, &st) == 0) ? 1 : 0;
}

int wwn_safety_path_b_armed(void) {
  struct stat st;
  return (stat(WWN_PATHB_PLIST, &st) == 0) ? 1 : 0;
}

int wwn_safety_dual_path_armed(void) {
  return (wwn_safety_path_a_armed() && wwn_safety_path_b_armed()) ? 1 : 0;
}

int wwn_safety_apple_job_disabled(void) {
  FILE *fp = popen("/bin/launchctl print-disabled system 2>/dev/null", "r");
  if (!fp)
    return 0;
  char line[512];
  int disabled = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (strstr(line, "com.apple.watchdogd") == NULL)
      continue;
    if (strstr(line, "disabled") != NULL)
      disabled = 1;
    break;
  }
  pclose(fp);
  return disabled;
}

int wwn_safety_reboot_successor_ok(void) {
  if (!wwn_safety_apple_job_disabled())
    return 1; /* Apple will load at next boot */
  /* Persist-disabled: need our staged successor. */
  return (wwn_safety_path_a_armed() || wwn_safety_path_b_armed()) ? 1 : 0;
}

static uint32_t be32(uint32_t v, int swap) {
  return swap ? OSSwapInt32(v) : v;
}

static int mh64_is_arm64e(const uint8_t *p, size_t n, int swap) {
  if (n < sizeof(struct mach_header_64))
    return 0;
  uint32_t cputype, subtype;
  memcpy(&cputype, p + 4, 4);
  memcpy(&subtype, p + 8, 4);
  cputype = be32(cputype, swap);
  subtype = be32(subtype, swap);
  if (cputype != (uint32_t)CPU_TYPE_ARM64 &&
      (cputype & ~CPU_ARCH_MASK) !=
          (uint32_t)(CPU_TYPE_ARM64 & ~CPU_ARCH_MASK))
    return 0;
  return ((subtype & ~CPU_SUBTYPE_MASK) == (uint32_t)CPU_SUBTYPE_ARM64E) ? 1
                                                                        : 0;
}

int wwn_safety_hook_is_arm64e(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 0;
  }
  long sz = ftell(f);
  if (sz < 32 || sz > 64 * 1024 * 1024) {
    fclose(f);
    return 0;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  uint8_t *all = malloc((size_t)sz);
  if (!all) {
    fclose(f);
    return 0;
  }
  if (fread(all, 1, (size_t)sz, f) != (size_t)sz) {
    free(all);
    fclose(f);
    return 0;
  }
  fclose(f);

  uint32_t magic;
  memcpy(&magic, all, 4);
  int ok = 0;
  if (magic == MH_MAGIC_64)
    ok = mh64_is_arm64e(all, (size_t)sz, 0);
  else if (magic == MH_CIGAM_64)
    ok = mh64_is_arm64e(all, (size_t)sz, 1);
  else if (magic == FAT_MAGIC || magic == FAT_CIGAM) {
    int swap = (magic == FAT_CIGAM);
    uint32_t nfat;
    memcpy(&nfat, all + 4, 4);
    nfat = be32(nfat, swap);
    size_t off = sizeof(struct fat_header);
    for (uint32_t i = 0; i < nfat; i++) {
      if (off + sizeof(struct fat_arch) > (size_t)sz)
        break;
      uint32_t offset, size;
      memcpy(&offset, all + off + 8, 4);
      memcpy(&size, all + off + 12, 4);
      offset = be32(offset, swap);
      size = be32(size, swap);
      off += sizeof(struct fat_arch);
      if ((size_t)offset + 16 > (size_t)sz)
        continue;
      uint32_t sm;
      memcpy(&sm, all + offset, 4);
      if (sm == MH_MAGIC_64 &&
          mh64_is_arm64e(all + offset, (size_t)sz - offset, 0)) {
        ok = 1;
        break;
      }
      if (sm == MH_CIGAM_64 &&
          mh64_is_arm64e(all + offset, (size_t)sz - offset, 1)) {
        ok = 1;
        break;
      }
      (void)size;
    }
  }
  free(all);
  return ok;
}

int wwn_safety_hook_codesign_ok(const char *path) {
  if (!path || !path[0])
    return 0;
  char cmd[768];
  /* Ad-hoc signed hooks are OK; reject unsigned / invalid. */
  snprintf(cmd, sizeof(cmd), "/usr/bin/codesign -v '%s' 2>/dev/null", path);
  int st = system(cmd);
  if (st == -1)
    return 0;
  return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 1 : 0;
}

int wwn_watchdogd_job_disable_verified(void) {
  if (wwn_watchdogd_job_disable() != 0)
    return -1;
  for (int i = 0; i < 20; i++) {
    if (wwn_safety_apple_job_disabled())
      return 0;
    usleep(100000);
  }
  fprintf(stderr,
          "wwn-safety: disable verified FAILED (print-disabled still "
          "enabled)\n");
  return -1;
}

int wwn_safety_arm_lock(void) {
  if (wwn_iow_db_mkdir() != 0)
    return -1;
  int fd = open(WWN_IOW_ARM_LOCK, O_CREAT | O_RDWR, 0600);
  if (fd < 0)
    return -1;
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    fprintf(stderr,
            "wwn-safety: another claim-install is in progress (%s)\n",
            WWN_IOW_ARM_LOCK);
    close(fd);
    return -1;
  }
  return fd;
}

void wwn_safety_arm_unlock(int fd) {
  if (fd < 0)
    return;
  (void)flock(fd, LOCK_UN);
  close(fd);
}

int wwn_safety_preflight_arm(enum wwn_safety_path path, const char *hook_path) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-safety: preflight: must run as root\n");
    return 1;
  }
  if (getenv("DYLD_INSERT_LIBRARIES") != NULL) {
    fprintf(stderr,
            "wwn-safety: preflight: refuse with DYLD_INSERT_LIBRARIES in "
            "environ (prefix on wrapper only)\n");
    return 2;
  }

  if (wwn_safety_dual_path_armed()) {
    fprintf(stderr,
            "wwn-safety: preflight: both Path A and Path B plists present. "
            "Run --heal or --uninstall first.\n");
    return 8;
  }

  /* Orphan persist-disable is a doctor/--heal concern. Arm may tear down
   * the other path first, which briefly looks like an orphan until new
   * plists are written; post-arm checks reboot_successor_ok. */

  if (!wwn_watchdogd_process_alive()) {
    fprintf(stderr,
            "wwn-safety: preflight: no live /usr/libexec/watchdogd; "
            "refusing arm (would leave uncovered after persist-disable)\n");
    return 3;
  }

  if (path == WWN_SAFETY_PATH_A) {
    if (wwn_safety_path_b_armed()) {
      fprintf(stderr,
              "wwn-safety: preflight: Path B still armed (%s). "
              "Run --uninstall first.\n",
              WWN_PATHB_PLIST);
      return 4;
    }
    if (!wwn_safety_amfi_relaxed() && getenv("WWN_IOW_PATHA_FORCE") == NULL) {
      fprintf(stderr,
              "wwn-safety: preflight: Path A needs "
              "amfi_get_out_of_my_way=1 (or WWN_IOW_PATHA_FORCE=1)\n");
      return 5;
    }
    return 0;
  }

  if (path == WWN_SAFETY_PATH_B) {
    if (wwn_safety_path_a_armed()) {
      fprintf(stderr,
              "wwn-safety: preflight: Path A still armed (%s). "
              "Run --uninstall first.\n",
              WWN_CLAIM_PLIST);
      return 4;
    }
    if (!hook_path || !hook_path[0]) {
      fprintf(stderr, "wwn-safety: preflight: Path B needs hook path\n");
      return 6;
    }
    struct stat st;
    if (stat(hook_path, &st) != 0 || !S_ISREG(st.st_mode)) {
      fprintf(stderr, "wwn-safety: preflight: hook missing: %s\n", hook_path);
      return 6;
    }
    if (!wwn_safety_hook_is_arm64e(hook_path)) {
      fprintf(stderr, "wwn-safety: preflight: hook is not arm64e: %s\n",
              hook_path);
      return 7;
    }
    if (!wwn_safety_hook_codesign_ok(hook_path)) {
      fprintf(stderr,
              "wwn-safety: preflight: hook codesign -v failed: %s\n",
              hook_path);
      return 10;
    }
    return 0;
  }

  fprintf(stderr, "wwn-safety: preflight: unknown path\n");
  return 99;
}

static void stamp_coverage_fail(const char *why) {
  char buf[256];
  snprintf(buf, sizeof(buf), "coverage-fail why=%s pid_alive=0\n",
           why ? why : "?");
  (void)wwn_iow_db_mkdir();
  (void)wwn_iow_write_file(WWN_IOW_COVERAGE_FAIL, buf);
}

int wwn_safety_postflight(const char *why) {
  /* Dwell: kickstart oneshots exit 0 within ~1s (KeepAlive SuccessfulExit=false). */
  usleep(1500000);
  if (wwn_coverage_ok()) {
    wwn_iow_unlink_quiet(WWN_IOW_COVERAGE_FAIL);
    return 0;
  }
  fprintf(stderr,
          "wwn-safety: postflight: no live watchdogd (%s); restoring Apple\n",
          why ? why : "");
  (void)wwn_watchdogd_job_restore();
  usleep(1500000);
  if (wwn_coverage_ok()) {
    fprintf(stderr, "wwn-safety: postflight: coverage recovered\n");
    wwn_iow_unlink_quiet(WWN_IOW_COVERAGE_FAIL);
    return 0;
  }
  fprintf(stderr,
          "wwn-safety: postflight: FAIL still no /usr/libexec/watchdogd\n");
  stamp_coverage_fail(why);
  return 1;
}

static int bootout_label(const char *label) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
           "/bin/launchctl bootout system/%s 2>/dev/null", label);
  (void)system(cmd);
  return 0;
}

int wwn_safety_heal(void) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-safety: --heal needs root\n");
    return 1;
  }
  fprintf(stderr, "wwn-safety: heal: tear down Path A/B; restore Apple\n");
  /* Enable Apple first so restore can load the job after Path B bootout. */
  (void)wwn_watchdogd_job_enable();
  bootout_label("com.aspauldingcode.wwn-iowatchdog-claim");
  bootout_label("com.aspauldingcode.wwn-iowatchdog-restore");
  bootout_label("com.aspauldingcode.wwn-iowatchdog-pathb");
  unlink(WWN_CLAIM_PLIST);
  unlink(WWN_RESTORE_PLIST);
  unlink(WWN_PATHB_PLIST);
  unlink(WWN_IOW_CLAIM_MARKER);
  wwn_iow_unlink_quiet(WWN_IOW_CLAIM_PENDING);
  /* Archive claim-ok: keeping the live stamp after heal lets product Take
   * Over treat a stale file as sticky while plain Apple watchdogd is armed
   * (2026-08-20 evening SIGTRAP panic). */
  if (rename(WWN_IOW_CLAIM_OK_STAMP, WWN_IOW_CLAIM_OK_STAMP ".last") != 0)
    wwn_iow_unlink_quiet(WWN_IOW_CLAIM_OK_STAMP);
  wwn_iow_unlink_quiet(WWN_IOW_SOCK_PATH);
  wwn_iow_unlink_quiet(WWN_IOW_DISABLED_MARKER);
  /*
   * Path B's hooked watchdogd may still be exiting. Starting Apple's
   * daemon beside it is SIGTRAP class. Wait until the binary is gone.
   */
  if (wwn_watchdogd_wait_gone(5000) != 0) {
    fprintf(stderr,
            "wwn-safety: heal HARD FAIL: previous watchdogd still alive "
            "after Path A/B bootout; not starting a second Apple daemon\n");
    stamp_coverage_fail("heal-dual-watchdogd");
    return 2;
  }
  if (wwn_watchdogd_job_restore() != 0 ||
      wwn_safety_postflight("heal") != 0) {
    fprintf(stderr, "wwn-safety: heal HARD FAIL: still uncovered\n");
    return 2;
  }
  if (!wwn_safety_reboot_successor_ok()) {
    fprintf(stderr,
            "wwn-safety: heal HARD FAIL: Apple still persist-disabled\n");
    stamp_coverage_fail("heal-orphan-disable");
    return 3;
  }
  wwn_iow_unlink_quiet(WWN_IOW_COVERAGE_FAIL);
  fprintf(stderr, "wwn-safety: heal OK (coverage live; Apple enabled)\n");
  return 0;
}

int wwn_safety_doctor(void) {
  char boot[1024];
  size_t len = sizeof(boot);
  if (sysctlbyname("kern.bootargs", boot, &len, NULL, 0) != 0)
    snprintf(boot, sizeof(boot), "(sysctl failed)");
  else {
    if (len >= sizeof(boot))
      len = sizeof(boot) - 1;
    boot[len] = '\0';
  }

  pid_t pid = wwn_watchdogd_find_pid();
  char pidbuf[32];
  if (pid > 0)
    snprintf(pidbuf, sizeof(pidbuf), "%d", (int)pid);
  else
    snprintf(pidbuf, sizeof(pidbuf), "(none)");

  struct stat st;
  int claim_ok = (stat(WWN_IOW_CLAIM_OK_STAMP, &st) == 0);
  int pending = (stat(WWN_IOW_CLAIM_PENDING, &st) == 0);
  int claim_held = (stat(WWN_IOW_CLAIM_MARKER, &st) == 0);
  int sock = wwn_sock_present();
  char sock_reply[128];
  int sock_live = 0;
  if (sock && wwn_sock_cmd("ping", sock_reply, sizeof(sock_reply)) == 0 &&
      strncmp(sock_reply, "OK", 2) == 0)
    sock_live = 1;

  int covfail = (stat(WWN_IOW_COVERAGE_FAIL, &st) == 0);
  int disabled_map = wwn_safety_apple_job_disabled();
  int path_a = wwn_safety_path_a_armed();
  int path_b = wwn_safety_path_b_armed();
  int dual = wwn_safety_dual_path_armed();
  int succ = wwn_safety_reboot_successor_ok();
  int cov = wwn_coverage_ok();

  int claim_ok_path_b = 0;
  if (claim_ok) {
    FILE *cf = fopen(WWN_IOW_CLAIM_OK_STAMP, "r");
    if (cf) {
      char line[128];
      if (fgets(line, sizeof(line), cf) && strstr(line, "path=b") != NULL)
        claim_ok_path_b = 1;
      fclose(cf);
    }
  }

  int stale_held = 0;
  if (claim_held) {
    FILE *hf = fopen(WWN_IOW_CLAIM_MARKER, "r");
    if (hf) {
      char line[128];
      int held_pid = -1;
      if (fgets(line, sizeof(line), hf) &&
          sscanf(line, "pid=%d", &held_pid) == 1 && held_pid > 0) {
        if (kill((pid_t)held_pid, 0) != 0 && errno == ESRCH)
          stale_held = 1;
      }
      fclose(hf);
    }
  }

  /*
   * Stale Path B (2026-08-20): claim-ok path=b + Path B plist, but Apple
   * watchdogd re-enabled and sock dead. Second watchdogd SIGTRAPs; doctor
   * used to say coverage_ok because any /usr/libexec/watchdogd counted.
   */
  int stale_path_b = 0;
  if (claim_ok_path_b && path_b && !sock_live && !disabled_map)
    stale_path_b = 1;

  printf("wwn-iowatchdog doctor\n");
  printf("  kern.bootargs: %s\n", boot);
  printf("  amfi_relaxed: %s\n", wwn_safety_amfi_relaxed() ? "yes" : "no");
  printf("  apple_watchdogd_disabled_map: %s\n",
         disabled_map ? "yes" : "no");
  printf("  path_a_plist: %s\n", path_a ? "yes" : "no");
  printf("  path_b_plist: %s\n", path_b ? "yes" : "no");
  printf("  dual_path: %s\n", dual ? "YES (illegal)" : "no");
  printf("  reboot_successor_ok: %s\n", succ ? "yes" : "NO");
  printf("  claim-pending: %s\n", pending ? "yes" : "no");
  printf("  claim-ok: %s%s\n", claim_ok ? "yes" : "no",
         claim_ok_path_b ? " (path=b)" : "");
  printf("  claim-held: %s%s\n", claim_held ? "yes" : "no",
         stale_held ? " (STALE pid)" : "");
  printf("  pathb_sock: %s%s\n", sock ? "yes" : "no",
         sock ? (sock_live ? " (live)" : " (STALE inode)") : "");
  printf("  coverage-fail stamp: %s\n", covfail ? "yes" : "no");
  printf("  /usr/libexec/watchdogd pid: %s\n", pidbuf);
  printf("  coverage_ok: %s\n", cov ? "yes" : "NO");
  fflush(stdout);

  int fail = 0;
  if (!cov) {
    fprintf(stderr, "wwn-safety: doctor: FAIL uncovered (no live watchdogd)\n");
    fail = 1;
  }
  if (!succ) {
    fprintf(stderr,
            "wwn-safety: doctor: FAIL orphan persist-disable "
            "(no Path A/B successor for next boot). Run --heal\n");
    fail = 1;
  }
  if (dual) {
    fprintf(stderr,
            "wwn-safety: doctor: FAIL dual Path A+B plists. Run --heal\n");
    fail = 1;
  }
  if (stale_path_b) {
    fprintf(stderr,
            "wwn-safety: doctor: FAIL stale Path B "
            "(claim-ok path=b, sock dead, Apple WD enabled). "
            "Run --heal then --path-b and reboot before Classic\n");
    fail = 1;
  }
  if (stale_held) {
    fprintf(stderr,
            "wwn-safety: doctor: WARN stale claim-held marker (pid dead)\n");
  }
  if (disabled_map && (path_a || path_b) && cov) {
    fprintf(stderr,
            "wwn-safety: doctor: NOTE armed for next boot "
            "(Apple disabled; successor staged; process live this session)\n");
  }
  return fail;
}
