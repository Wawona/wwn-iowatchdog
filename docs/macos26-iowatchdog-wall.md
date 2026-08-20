# macOS 26 IOWatchdog wall (25F80)

Investigation notes for Mode B Take Over. SIP fully disabled,
`-arm64e_preview_abi` present. Goal: call
`DisableUserspaceMonitoring` (selector 3) on the live
`IOWatchdogUserClient` held by `/usr/libexec/watchdogd` without
lldb (lldb attach exits watchdogd with SIGTRAP and panics).

## What works

| Primitive | Result |
|-----------|--------|
| `task_for_pid(watchdogd)` as root | OK |
| `mach_port_names` + `mach_port_kobject_description` | Finds `IOWatchdogUserClient` (e.g. name `0x1107`) |
| `thread_suspend` / `thread_get_state` / `thread_resume` (no set_state) | OK |
| `mach_vm_allocate` / `mach_vm_read` in watchdogd | OK |
| Build `libwwn_watchdogd_hook.dylib` as **arm64e** | OK |

## What is blocked (caller or target)

| Primitive | Result |
|-----------|--------|
| `IOServiceOpen(..., type=1)` while watchdogd holds client | `exclusive access and device already open` |
| `mach_port_extract_right` on IOWatchdog send right | `KERN_INVALID_CAPABILITY` |
| `thread_set_state` on any watchdogd thread (even scratch xN) | **Caller SIGKILL** (exit 137). watchdogd survives |
| `thread_create_running` into watchdogd | `KERN_PROTECTION_FAILURE` |
| `mach_vm_protect` / `mach_vm_write` / `mach_vm_remap` on auth-ptr GOT slot | `protection failure` / `invalid address` |
| lldb / `lldb_mcp` attach | watchdogd SIGTRAP → kernel panic |

## Consequence

`wwn-iowatchdog disable|enable` stay **fail closed** on 25F80.
Wawona Take Over stays `WWN_MODEB_WD=blocked-no-iowatchdog`.
Do not bootout `com.apple.watchdogd` without a successful disable ACK.

## Possible future paths (not implemented)

- Additional boot-args / AMFI relax beyond current SIP-off (unproven).
- WindowServer mute without unloading watchdogd (plan: out of scope).
- Apple-signed platform inject entitlement (not available to third parties).

## Hook dylib

`libwwn_watchdogd_hook.dylib` (arm64e) remains in the package for a
future inject primitive. It must not be loaded via lldb.
