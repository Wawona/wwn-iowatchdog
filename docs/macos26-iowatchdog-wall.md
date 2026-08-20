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

- Prove Path A claim across reboot (sticky marker + 60s). Checklist in wall.
- Prove Path B sock after a non-lldb hook load.
- Then a separate plan for Wawona Take Over flip.
- Do not re-run `thread_set_state` / lldb on the daily driver.
- Java ≥17 HotSpot on 25F80 still SIGBUS; revisit for Ghidra headless.

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

## GhidraVibe RE (2026-08-20, updated same day)

### Toolchain

| Item | Status |
|------|--------|
| Cursor MCP (`ghidra` / `ghidra-vibe` / `ghidra-vibe-rag`) | **Fixed** via nix-darwin `.dotfiles` → `~/GhidraVibe#ghidra-vibe` uv wrappers |
| Headless Ghidra 12 (needs Java 21) | **Blocked on this host**: Zulu 21, Temurin 21, and Temurin 17 all SIGBUS in `CodeHeap::allocate` (`BUS_ADRALN`). MS OpenJDK 11 works. ~2 GiB free RAM; not a memory-pressure issue. Decompile via **ipsw + llvm-objdump** instead |
| Project import / MCP decompile | Deferred until a working HotSpot ≥17 exists on 25F80 |

### Artifacts under `/tmp/wwn-re/`

- Thin userspace: `watchdogd.arm64e`, `sysstatuscheck.arm64e`, `mobile_obliterator.arm64e`, `WindowServer.arm64e`
- KC: `kc/kernelcache.decompressed` + extracted `kc/com.apple.driver.AppleARMWatchdogTimer` (via `ipsw kernel extract`, UUID `BDBB2E95-…`, source 333.0.0.0.0)

### Userspace (llvm-objdump)

| Target | Finding |
|--------|---------|
| `watchdogd` | `IOServiceOpen(IOWatchdog, type=1)`. Selectors: CheckEnabled=0, **Checkin=1**, Disable=3, Reenable=4, CheckUserspaceDefanged=5 |
| `sysstatuscheck` | Same open type=1; Checkin=1 only |
| `mobile_obliterator` | No IOWatchdog strings on this 25F80 build |
| WindowServer | No direct IOWatchdog open; watchdogd polls service checkins |

### Kext (`IOWatchdog::newUserClient` / disable / close / checkWatchdog)

1. **Exclusive / seize:** type must be `1`. Existing client pointer at object `+0x98` non-null → return `0xe00002c5` (exclusive). **No seize / second client.** Root privilege + `copyClientEntitlement(…, "com.apple.private.iowatchdog.user-access")` required (enforced in kext `newUserClient`).
2. **Disable sticky?** **Yes.** `userspaceDisableUserspaceMonitoring` clears flag at `+0xa8` and timer state at `+0xb0`. `userClientClose` only stores null to `+0x98` (frees exclusive). Does **not** restore `+0xa8`. `userspaceCheckin` does **not** re-set `+0xa8`.
3. **`checkWatchdog`:** `ldrb [obj,#0xa8]`; if clear, early-out (no userspace timeout panic path). So sticky disable survives client close and later watchdogd open/checkin.
4. **Entitlement:** string `com.apple.private.iowatchdog.user-access` checked in-kext via `IOUserClient::copyClientEntitlement` after a `"root"` privilege check. AMFI still gates whether the entitlement is present on the task; forge works under SIP-off + AMFI-relaxed as previously verified.
5. **Defang:** separate refcount path (`increaseDefangRefCount` / `toggleUserSpaceMonitoringWithReason`); not required for Classic disable.

### Design consequence

- Path A claim default is **sticky-release**: open → disable → close → exit (no Reenable). `KeepAlive` false.
- `--hold` keeps exclusive until SIGTERM (still no Reenable on exit).
- Path B still needs a non-lldb hook load into watchdogd for the Unix sock; sticky disable alone does not load the hook.

## Dual-path (0.3.x)

| Path | Mechanism | When it works |
|------|-----------|---------------|
| **A direct** | Entitled `IOServiceOpen` type=1 + sel 3/4 | Client free |
| **A claim** | Boot LaunchDaemon: disable then sticky-release (or `--hold`) | Wins race before watchdogd |
| **B sock** | Unix socket to `libwwn_watchdogd_hook.dylib` | Hook loaded; live soft-inject fail-closed |
| Live soft-inject | `thread_set_state` / GOT | **Blocked** (Phase 1.4) |

### Proof gates

1. Path A claim across reboot (sticky marker + 60s, no panic) — **human-gated** (see checklist below).
2. Path B sock round-trip 60s after non-lldb hook load — **open**.
3. Wawona Take Over flip — **separate plan only**; stays `blocked-no-iowatchdog`.

### Claim reboot checklist (p1; do not auto-reboot)

```text
1. pgrep -lf lldb_mcp || echo lldb_mcp_gone   # must be gone
2. nix build ./wwn-iowatchdog#wwn-iowatchdog
3. sudo ./result/bin/wwn-iowatchdog claim-install
4. sudo launchctl bootstrap system \
     /Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-claim.plist
5. Reboot (user-approved only)
6. After login: sudo ./result/bin/wwn-iowatchdog status
   Expect: marker=yes, pathA may be exclusive|free, claim file or sticky marker
7. Hold 60s; confirm no panic; cat marker
8. Optional: sudo ./result/bin/wwn-iowatchdog enable  # only when done testing
```

## Hook dylib

`libwwn_watchdogd_hook.dylib` (arm64e) is Path B. Load only after disable ACK via a future boot-time path; never via lldb.
