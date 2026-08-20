/*
 * Soft helpers for wwn-iowatchdog (Desktop Mode B).
 *
 * status: locate IOWatchdogUserClient port name inside live watchdogd via
 * mach_port_kobject_description (works on 25F80).
 *
 * disable/enable: intended path was thread-hijack or GOT trampoline inside
 * watchdogd. On macOS 26 / 25F80 those are blocked:
 *   - thread_set_state(watchdogd) => caller SIGKILL
 *   - mach_port_extract_right(IOKit) => KERN_INVALID_CAPABILITY
 *   - mach_vm_protect/write on auth-ptr GOT => protection failure
 * So disable/enable fail closed (never lldb). Take Over stays blocked until
 * a working primitive exists.
 */
#include "../common/wwn_iowatchdog.h"

#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/mach_port.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
          "wwn-iowatchdog: disable/enable blocked on this macOS build.\n"
          "  Port steal (extract_right) = KERN_INVALID_CAPABILITY.\n"
          "  thread_set_state(watchdogd) = caller SIGKILL.\n"
          "  GOT/auth-ptr patch = protection failure.\n"
          "  lldb attach = panic (forbidden).\n"
          "  Take Over must stay blocked. See docs/macos26-iowatchdog-wall.md\n");
  return -1;
}

int wwn_watchdogd_inject(const char *dylib_path) {
  (void)dylib_path;
  fprintf(stderr,
          "wwn-iowatchdog inject: blocked (thread_set_state SIGKILL on "
          "watchdogd). Hook dylib is built for a future inject primitive.\n");
  return 1;
}
