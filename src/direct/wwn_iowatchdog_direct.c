/*
 * Path A: entitled direct IOServiceOpen(IOWatchdog, type=1) + scalar.
 *
 * Requires com.apple.private.iowatchdog.user-access (ad-hoc forge under
 * SIP-off + AMFI-relaxed). Does not unlock exclusive while watchdogd holds
 * the client (0xe00002c5). No seize API found on 25F80 (static RE).
 */
#include "../common/wwn_iowatchdog.h"

#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <stdio.h>
#include <string.h>

#ifndef kIOReturnExclusiveAccess
#define kIOReturnExclusiveAccess ((kern_return_t)0xe00002c5)
#endif

static io_connect_t wwn_direct_open(kern_return_t *out_kr) {
  *out_kr = KERN_FAILURE;
  io_service_t svc = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching(WWN_IOW_SERVICE_NAME));
  if (svc == IO_OBJECT_NULL) {
    fprintf(stderr, "wwn-iowatchdog: Path A: no IOWatchdog service\n");
    return IO_OBJECT_NULL;
  }
  io_connect_t conn = IO_OBJECT_NULL;
  kern_return_t kr =
      IOServiceOpen(svc, mach_task_self(), WWN_IOW_OPEN_TYPE, &conn);
  IOObjectRelease(svc);
  *out_kr = kr;
  if (kr != KERN_SUCCESS) {
    fprintf(stderr, "wwn-iowatchdog: Path A: IOServiceOpen type=%u: %s (0x%x)\n",
            WWN_IOW_OPEN_TYPE, mach_error_string(kr), (unsigned)kr);
    return IO_OBJECT_NULL;
  }
  return conn;
}

int wwn_direct_open_probe(void) {
  kern_return_t kr = KERN_FAILURE;
  io_connect_t conn = wwn_direct_open(&kr);
  if (conn != IO_OBJECT_NULL) {
    IOServiceClose(conn);
    return 0; /* free */
  }
  if (kr == kIOReturnExclusiveAccess)
    return 1; /* held */
  return -1;
}

int wwn_direct_scalar(uint32_t selector) {
  kern_return_t kr = KERN_FAILURE;
  io_connect_t conn = wwn_direct_open(&kr);
  if (conn == IO_OBJECT_NULL) {
    if (kr == kIOReturnExclusiveAccess)
      return 1;
    return -1;
  }
  uint32_t outCnt = 0;
  kr = IOConnectCallScalarMethod(conn, selector, NULL, 0, NULL, &outCnt);
  if (kr != KERN_SUCCESS) {
    fprintf(stderr,
            "wwn-iowatchdog: Path A: IOConnectCallScalarMethod sel=%u: %s "
            "(0x%x)\n",
            selector, mach_error_string(kr), (unsigned)kr);
    IOServiceClose(conn);
    return -1;
  }
  /* Close after one-shot; claim daemon holds separately. */
  IOServiceClose(conn);
  return 0;
}
