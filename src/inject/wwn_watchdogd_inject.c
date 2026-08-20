/*
 * Soft helpers for wwn-iowatchdog (Desktop Mode B).
 *
 * status: locate IOWatchdogUserClient port name inside live watchdogd via
 * mach_port_kobject_description (works on 25F80).
 *
 * Live soft-inject (thread_set_state / GOT) stays fail-closed on 25F80.
 * inject-launchd writes an opt-in LaunchDaemon EnvironmentVariables hint
 * only when the disable marker is present (Path A claim or Path B sock ACK).
 */
#include "../common/wwn_iowatchdog.h"

#include <errno.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/mach_port.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

static int lldb_mcp_running(void) {
  int n = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0) / (int)sizeof(pid_t);
  if (n <= 0)
    return 0;
  pid_t *pids = calloc((size_t)n, sizeof(pid_t));
  if (!pids)
    return 0;
  n = proc_listpids(PROC_ALL_PIDS, 0, pids, n * (int)sizeof(pid_t)) /
      (int)sizeof(pid_t);
  int found = 0;
  for (int i = 0; i < n; i++) {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (pids[i] <= 0)
      continue;
    if (proc_pidpath(pids[i], path, sizeof(path)) > 0 &&
        strstr(path, "lldb_mcp") != NULL) {
      found = 1;
      break;
    }
  }
  free(pids);
  return found;
}

int wwn_find_iowatchdog_port_name(pid_t pid, mach_port_name_t *out_name) {
  task_t task = MACH_PORT_NULL;
  kern_return_t kr = task_for_pid(mach_task_self(), (int)pid, &task);
  if (kr != KERN_SUCCESS)
    return -1;
  mach_port_name_array_t names = NULL;
  mach_port_type_array_t types = NULL;
  mach_msg_type_number_t namesCnt = 0, typesCnt = 0;
  kr = mach_port_names(task, &names, &namesCnt, &types, &typesCnt);
  if (kr != KERN_SUCCESS) {
    mach_port_deallocate(mach_task_self(), task);
    return -1;
  }
  int found = 0;
  for (mach_msg_type_number_t i = 0; i < namesCnt; i++) {
    natural_t kotype = 0;
    mach_vm_address_t kobject = 0;
    kobject_description_t desc;
    memset(desc, 0, sizeof(desc));
    kr = mach_port_kobject_description(task, names[i], &kotype, &kobject,
                                       desc);
    if (kr != KERN_SUCCESS)
      continue;
    if (strncmp(desc, "IOWatchdogUserClient", 20) == 0) {
      *out_name = names[i];
      found = 1;
      break;
    }
  }
  vm_deallocate(mach_task_self(), (vm_address_t)names,
                namesCnt * sizeof(mach_port_name_t));
  vm_deallocate(mach_task_self(), (vm_address_t)types,
                typesCnt * sizeof(mach_port_type_t));
  mach_port_deallocate(mach_task_self(), task);
  return found ? 0 : -1;
}

int wwn_remote_ioconnect_scalar(pid_t pid, mach_port_name_t port_name,
                                uint32_t selector) {
  (void)pid;
  (void)port_name;
  (void)selector;
  if (lldb_mcp_running()) {
    fprintf(stderr, "wwn-iowatchdog: refuse: lldb_mcp running\n");
    return -1;
  }
  fprintf(stderr,
          "wwn-iowatchdog: live soft-inject blocked on 25F80 "
          "(thread_set_state / GOT). Use Path A (direct/claim) or Path B "
          "(hook sock after inject-launchd). See "
          "docs/macos26-iowatchdog-wall.md\n");
  return -1;
}

int wwn_watchdogd_inject(const char *dylib_path) {
  (void)dylib_path;
  fprintf(stderr,
          "wwn-iowatchdog inject: blocked (thread_set_state SIGKILL on "
          "watchdogd). Use inject-launchd after a disable marker, or Path A "
          "claim.\n");
  return 1;
}

int wwn_inject_launchd(const char *dylib_path) {
  if (geteuid() != 0) {
    fprintf(stderr, "wwn-iowatchdog: inject-launchd needs root\n");
    return 1;
  }
  struct stat st;
  if (stat(WWN_IOW_DISABLED_MARKER, &st) != 0) {
    fprintf(stderr,
            "wwn-iowatchdog: inject-launchd refused: no disable marker at %s\n"
            "  Get Path A claim disable ACK (or Path B sock disable) first.\n",
            WWN_IOW_DISABLED_MARKER);
    return 2;
  }
  if (lldb_mcp_running()) {
    fprintf(stderr, "wwn-iowatchdog: refuse: lldb_mcp running\n");
    return 3;
  }

  const char *hook = dylib_path;
  char default_hook[512];
  if (!hook) {
    /* Prefer sibling of this binary: ../lib/libwwn_watchdogd_hook.dylib */
    char self[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(getpid(), self, sizeof(self)) <= 0) {
      fprintf(stderr, "wwn-iowatchdog: cannot resolve self path\n");
      return 4;
    }
    char *slash = strrchr(self, '/');
    if (!slash) {
      fprintf(stderr, "wwn-iowatchdog: bad self path\n");
      return 4;
    }
    *slash = '\0';
    snprintf(default_hook, sizeof(default_hook),
             "%s/../lib/%s", self, WWN_IOW_HOOK_NAME);
    hook = default_hook;
  }
  if (stat(hook, &st) != 0) {
    fprintf(stderr, "wwn-iowatchdog: hook missing: %s\n", hook);
    return 5;
  }

  /*
   * Write an opt-in overlay plist. Do NOT bootout/kickstart watchdogd here.
   * Operator must reboot (or carefully restart after disable ACK) so
   * DYLD_INSERT_LIBRARIES can apply. Live soft-inject stays fail-closed.
   */
  const char *plist =
      "/Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-inject.plist";
  FILE *f = fopen(plist, "w");
  if (!f) {
    fprintf(stderr, "wwn-iowatchdog: cannot write %s: %s\n", plist,
            strerror(errno));
    return 6;
  }
  fprintf(f,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n"
          "<dict>\n"
          "  <key>Label</key>\n"
          "  <string>com.aspauldingcode.wwn-iowatchdog-inject</string>\n"
          "  <key>ProgramArguments</key>\n"
          "  <array>\n"
          "    <string>/bin/echo</string>\n"
          "    <string>wwn-iowatchdog-inject overlay installed; reboot with "
          "disable ACK held so watchdogd can load hook via manual "
          "DYLD_INSERT_LIBRARIES override if platform allows</string>\n"
          "  </array>\n"
          "  <key>RunAtLoad</key>\n"
          "  <true/>\n"
          "  <key>EnvironmentVariables</key>\n"
          "  <dict>\n"
          "    <key>WWN_IOWATCHDOG_HOOK</key>\n"
          "    <string>%s</string>\n"
          "  </dict>\n"
          "</dict>\n"
          "</plist>\n",
          hook);
  fclose(f);
  fprintf(stderr,
          "wwn-iowatchdog: wrote %s (hook=%s).\n"
          "  Live inject remains fail-closed. This records the hook path for "
          "a future boot-time load after disable ACK.\n"
          "  Do not kickstart -k watchdogd.\n",
          plist, hook);
  return 0;
}
