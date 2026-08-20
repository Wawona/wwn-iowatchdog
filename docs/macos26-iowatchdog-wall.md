# macOS 26 IOWatchdog wall (25F80)

Investigation notes for Mode B **Classic** Take Over (unload WindowServer).
SIP fully disabled. Goal: call `DisableUserspaceMonitoring` (selector 3) on
the live `IOWatchdogUserClient` held by `/usr/libexec/watchdogd` without
lldb (lldb attach exits watchdogd with SIGTRAP and panics).

## Verdict (Phase 1.4)

**25F80 Classic blocked.** Boot-arg pass (`amfi_get_out_of_my_way=1` on top
of `-arm64e_preview_abi`) did **not** yield a proven
`DisableUserspaceMonitoring` path. Do not enable Settings Take Over. Do not
ship KEEP_WS mute as Classic. `wwn-iowatchdog disable|enable|inject` stay
fail closed. Wawona stays `WWN_MODEB_WD=blocked-no-iowatchdog`.

## What works

| Primitive | Result |
|-----------|--------|
| `task_for_pid(watchdogd)` as root | OK |
| `mach_port_names` + `mach_port_kobject_description` | Finds `IOWatchdogUserClient` |
| `thread_suspend` / `thread_get_state` / `thread_resume` (no set_state) | OK |
| `mach_vm_allocate` / `mach_vm_read` in watchdogd | OK |
| Build `libwwn_watchdogd_hook.dylib` as **arm64e** | OK |
| Some RO `mach_vm_protect(COPY)` + write-same under `amfi_get_out_of_my_way=1` | OK on **some** regions (not a proven IOConnect GOT hijack) |

## What is blocked (caller, target, or product)

| Primitive | Result |
|-----------|--------|
| `IOServiceOpen(..., type=1)` while watchdogd holds client | `exclusive access and device already open` |
| `mach_port_extract_right` on IOWatchdog send right | `KERN_INVALID_CAPABILITY` |
| `thread_set_state` on any watchdogd thread (even scratch xN) | Caller SIGKILL (137). **Panic-class risk** (see incident) |
| `thread_create_running` into watchdogd | `KERN_PROTECTION_FAILURE` |
| Auth-ptr / IOConnect-targeted GOT patch → working `disable` | **Not proven** on 25F80 (many RO regions still protection-failure; no disable ACK) |
| lldb / `lldb_mcp` attach | watchdogd SIGTRAP → kernel panic |
| Classic Take Over product flip | **Stopped** (this doc, Phase 1.4) |

## Phase 1 boot-arg pass (2026-08-20)

1. Stock SIP-off + `-arm64e_preview_abi`: GOT/auth-ptr patch and
   `thread_set_state` blocked as in the table above.
2. Additive `amfi_get_out_of_my_way=1`: after reboot, **GOT-only** retest
   (no `thread_set_state`): some read-only regions accept COW write-same;
   other RO regions still `protection failure`. No
   `sudo wwn-iowatchdog disable` OK / marker. Not an unlock for Classic.
3. **Do not** re-run `thread_set_state` probe loops on the daily driver.

### Incident during Phase 1

- Panic evidence: `panic-full-2026-08-20-135756.0002.panic` /
  `.contents.panic`:
  `watchdogd[…] exited -- exit reason namespace 2 subcode 0x5`.
- Later reboot while Phase 1 work was in flight. Temporary
  `/etc/sudoers.d/wwn-phase1` and `/tmp/wwn-phase1-*` removed.
- Prefer `kern.bootargs` without `amfi_get_out_of_my_way=1` unless a new
  named experiment needs it (revert is next-reboot).

## Consequence

| Gate | State |
|------|--------|
| Phase 1 disable ACK | **FAIL** (no working primitive) |
| Phase 2 proofs (bootout / Classic smoke) | **ABORTED** (requires Phase 1 OK) |
| Phase 3 Settings Take Over | **NOT FLIPPED** |
| `WWN_MODEB_WD` | `blocked-no-iowatchdog` |

Do not bootout `com.apple.watchdogd` without a successful disable ACK.

## Possible future paths

- Prove Path A claim across reboot (60s hold).
- Prove Path B sock after a non-lldb hook load.
- Then a separate plan for Wawona Take Over flip.
- Do not re-run `thread_set_state` / lldb on the daily driver.

## CoreBedtime research (2026-08-20, KEEP_WS only)

Upstream [CoreBedtime/iland](https://github.com/CoreBedtime/iland)
`install-weston.sh` is identical to the vendored copy: last step is
`launchctl unload -w …/com.apple.WindowServer.plist`. Repo search finds
**zero** `watchdogd` / `IOWatchdog` / `iowatchdog` handling.

| Test | Result |
|------|--------|
| Full Classic (`install-weston.sh` unload WS) | **Not run** on 25F80 (predicted 120s userspace-watchdog panic; same class as prior Mode B incidents) |
| KEEP_WS inject: `DYLD_INSERT_LIBRARIES=libwayland-mac.dylib` + compositor | **Constructor OK**: extracts `/tmp/libwayland-support/{framebufferd,inputd,amfiexceptiond}`, hooks amfid, starts `framebufferd` (CoreDisplay / in-server present). `WindowServer` + `watchdogd` stayed up; no panic |
| Bundled `weston --backend=drm` (wwn-weston result) | Exits: missing `drm-backend.so` store path |
| Bundled Wawona `niri` + insert | Exits: Wawona niri refuses bare host Wayland (`no host Wayland display`); helpers still spawned |

Conclusion: CoreBedtime’s **inject/present** model still works under KEEP_WS on
25F80. Their **Classic unload-WS** path has no IOWatchdog disable and is
not safer than Wawona’s blocked Take Over on this OS. Does not unblock
Phase 1.

## GhidraVibe RE (2026-08-20)

Headless Ghidra 12.1 on this machine hit **Java 21 Zulu SIGBUS**
(`BUS_ADRALN` in CodeHeap) even after freeing RAM. MS OpenJDK 11 runs;
Ghidra 12 wants 21. Cursor `~/.cursor/mcp.json` is **home-manager managed**
and still pointed at missing `GhidraMCP_Vibe_RSE`. Bridges are built at
`~/GhidraVibe/result-ghidra-vibe-fresh/share/ghidra-mcp/`; rewrite HM MCP
entries to those absolute uv bridges when HM is next switched. RE below is
from **llvm-objdump / strings** on thin arm64e slices under `/tmp/wwn-re/`.

| Target | Finding |
|--------|---------|
| `AppleARMWatchdogTimer.kext` | Bundle on disk has **no** `Contents/MacOS` executable (KC-resident only). No userspace seize API visible without KC extract. |
| `/usr/libexec/watchdogd` | `IOServiceOpen(IOWatchdog, type=1)`. Selectors on 25F80: CheckEnabled=0, **Checkin=1**, DisableUserspaceMonitoring=3, Reenable=4, CheckUserspaceDefanged=5. Holds exclusive client for life of process. |
| `/usr/libexec/sysstatuscheck` | Same open type=1; Checkin sel=1. No disable path. Confirms exclusive + entitlement class. |
| `/usr/libexec/mobile_obliterator` | **No** `IOWatchdog` / `DisableUserspace` strings on this 25F80 build (earlier research applied to other OS/images). Do not invoke as a helper. |
| WindowServer / SkyLight / loginwindow | No direct `IOWatchdog` user client open found. Userspace monitoring is **watchdogd** polling service checkins; unloading WS without sel=3 still yields the ~120s userspace-watchdog panic class. |

### Answers to plan questions

1. **Exclusive policy:** type=1 is single-client. Live open while watchdogd holds returns `0xe00002c5`. No second-client / seize found in userspace RE. Default: **claim-hold**.
2. **Disable sticky?** Unproven without kext. Claim daemon re-enables on exit before close.
3. **Entitlement:** `com.apple.private.iowatchdog.user-access` required for open (forged ad-hoc under SIP-off + AMFI-relaxed works; does not beat exclusive).
4. **Obliterator:** not present as IOWatchdog helper on this build.
5. **WindowServer checkin:** via watchdogd service monitoring, not a direct IOWatchdog open in WS. Classic unload still requires disable ACK first.

## Dual-path (0.3.0)

| Path | Mechanism | When it works |
|------|-----------|---------------|
| **A direct** | Entitled `IOServiceOpen` type=1 + sel 3/4 | Client free (no watchdogd hold) |
| **A claim** | Opt-in LaunchDaemon `wwn-iowatchdog-claim` opens, disables, **holds** | Boot race before watchdogd |
| **B sock** | Unix socket to `libwwn_watchdogd_hook.dylib` in watchdogd | Hook loaded; live soft-inject still fail-closed |
| Live soft-inject | `thread_set_state` / GOT | **Blocked** (Phase 1.4) |

CLI: `disable`/`enable` try A then B. `status` reports pathA/sock/marker/claim.
`inject-launchd` only with disable marker. **Wawona Take Over stays
`blocked-no-iowatchdog` until proof gates.**

### Proof gates (not yet run)

1. Path A stable 60s with claim held (reboot after `claim-install`).
2. Path B sock round-trip 60s after hook is actually loaded.
3. Separate plan before flipping Settings Take Over.

## Hook dylib

`libwwn_watchdogd_hook.dylib` (arm64e) is Path B. Load only after disable
ACK (claim) via a future boot-time path; never via lldb.
