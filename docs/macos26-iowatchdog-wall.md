# macOS 26 IOWatchdog wall (25F80)

Investigation notes for Mode B **Classic** Take Over (unload WindowServer).
SIP fully disabled. Goal: call `DisableUserspaceMonitoring` (selector 3) on
the live `IOWatchdogUserClient` held by `/usr/libexec/watchdogd` without
lldb (lldb attach exits watchdogd with SIGTRAP and panics).

## Verdict (Phase 1.4 + Phase 2; updated 0.3.8)

**Path B reboot sticky: PROVEN** (2026-08-20). `claim-ok` =
`ok path=b sticky=1 replacee=1`, marker `path-b-auto`, sock
`done=1 dkr=0x0`, pathb LaunchDaemon stable.

**Path A:** `amfi_get_out_of_my_way=1` + Disable with **`outCnt=0`**.
Interactive sticky **proven** 2026-08-20 (`ok path=a sticky=1`, marker
`path-a-sticky`). Reboot RunAtLoad proof: stage plists only (0.3.9).

### Load-path RE (2026-08-20 late → 0.3.8)

| Failure | Root cause | Fix |
|---------|------------|-----|
| Path B SIGBUS 138 | **fishhook** GOT patch on arm64e PAC slots | **0.3.6:** drop fishhook; use `DYLD_INTERPOSE` |
| Path B SIGSEGV recursion | `dlsym` / `dlopen(IOKit)` returns the **interpose** (hook) | **0.3.7:** call `__DATA,__interpose` **replacee** |
| Path A `OS_REASON_CODESIGNING` / 137 | AMFI rejects ad-hoc forged `com.apple.private.iowatchdog.user-access` | **0.3.8:** `--path-a` + `--path-a-amfi-nvram`; refuse arm without AMFI unless `WWN_IOW_PATHA_FORCE=1` |

**Lab sticky (Path B, sole + reboot):** replacee auto-disable ACK.
**Lab sticky (Path A, AMFI + outCnt=0):** interactive + **reboot RunAtLoad**
ACK proven (`ok path=a sticky=1`, `path-a-sticky`).

IOWatchdog kext RE unchanged (Checkin=1, Disable=3, sticky `+0xa8`).

Take Over may leave `blocked-no-iowatchdog` once product wiring consumes
the Path B ACK (separate change).

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

## Consequence (updated 0.3.8)

| Gate | State |
|------|--------|
| Path B reboot sticky | **PASS** (see [`path-a-path-b.md`](path-a-path-b.md)) |
| Path A claim (no AMFI) | **FAIL** (codesigning / 137) |
| Path A claim (`amfi_get_out_of_my_way=1`) | **PASS** interactive + reboot sticky (0.3.9) |
| Soft-inject / `thread_set_state` | **FAIL closed** |
| Phase 3 Settings Take Over | **HOLD** after 2026-08-20 evening panic: require live Disable (marker/sock `done=1`) + claim-ok; stage must not re-enable Apple while Path B armed. Incident: [`Wawona/docs/incident-reports/2026-08-20-stale-claim-ok-takeover/`](../../Wawona/docs/incident-reports/2026-08-20-stale-claim-ok-takeover/) |
| Operator how-to | [`path-a-path-b.md`](path-a-path-b.md) |
| Safety guards (coverage) | [`path-a-path-b.md`](path-a-path-b.md#safety-guards-0310) (`--doctor` / `--heal`) |

Do not bootout `com.apple.watchdogd` without a successful disable ACK.
Do not use lldb on `watchdogd`.

## Possible future paths

- Operator guide (Path A/B arm + markers): [`path-a-path-b.md`](path-a-path-b.md).
- Named experiment: `amfi_get_out_of_my_way=1` + Path A claim reboot proof.
  Revert boot-arg after.
- Product Take Over flip once Wawona consumes Path B `claim-ok`.
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

## GhidraVibe RE (2026-08-20, updated evening)

### Toolchain

| Item | Status |
|------|--------|
| Cursor MCP stdio (`ghidra-mcp` / `ghidra-vibe-mcp` / `ghidra-vibe-rag-mcp`) | **Live** via nix `#ghidra-vibe-mcp` (same host model as mcp-nixos / wwn-mcp). No public URL. Vibe auto-starts mcp-ext (70 tools). |
| Headless Ghidra analysis (`:8089`) | **Still blocked**: Zulu 21 SIGBUS in `CodeHeap::allocate`. MS OpenJDK 11 only other JVM. `list_instances` empty. |
| Decompile path used | **ipsw + llvm-objdump** on `/tmp/wwn-re/kc/com.apple.driver.AppleARMWatchdogTimer`. Slices in `/tmp/wwn-re/kc/disasm/*.s`. |
| Safety this session | Killed stray `lldb_mcp.py` before RE. Loop PID 28701 still armed. |

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

### Kext instruction-level (25F80 `IOWatchdog`)

Evidence: `/tmp/wwn-re/kc/disasm/{newUserClient,userClientClose,userspaceDisable,userspaceCheckin,userspaceReenable,checkWatchdog}.s`.

1. **Exclusive / seize (`newUserClient` @ `…48408`):** `cmp w24,#1`. Wrong type → `0xe00002c2`. Type 1 + `[obj+0x98]!=0` → **`0xe00002c5`**. No seize. Entitlement string `com.apple.private.iowatchdog.user-access` in kext; `"root"` then `copyClientEntitlement`. After open, if `+0xa8` bit0 clear, skips deadline arm (`+0xb0`/`+0xc8`): sticky is not undone by a later open.
2. **Disable sticky (`userspaceDisable…` @ `…48868`):** `strb wzr,[obj,#0xa8]` then `stp q0,q0,[obj,#0xb0]`. Returns 0.
3. **Close (`userClientClose` @ `…48614`):** only `str xzr,[obj,#0x98]`. Does not touch `+0xa8`.
4. **Checkin (`userspaceCheckin` @ `…48788`):** writes `+0xab=1` / counters. Does **not** write `+0xa8`.
5. **Reenable (`userspaceReenable…` @ `…488e4`):** sole `strb #1,[obj,#0xa8]` path.
6. **`checkWatchdog` (@ `…481a8`):** `ldrb` `+0xa8` then `tbz` early-out before `shutdownCheckWatchdog`.
7. **Defang:** separate; not required for Classic disable.

### launchd note (Path B)

`com.apple.watchdogd`: `KeepAlive.SuccessfulExit=false`;
`_PanicOnCrash.PanicOnConsecutiveCrash=true`; IOKit-matched start of
`/usr/libexec/watchdogd`. SIP fully disabled here. Hook constructor must
not crash. Prefer boot-time `DYLD_INSERT` after sticky claim. Current
`inject-launchd` is echo-only (records hook path; does not attach insert).
`launchctl kickstart` without `-k` is a oneshot here: watchdogd starts,
exits 0, launchd does not restart it. Heal must re-register LaunchEvents
(bootout+bootstrap while the process is already gone) and dwell until
the daemon stays up. Never `kickstart -k`. Never bootout a live watchdogd.

### Design consequence

- Path A claim default is **sticky-release**: open → disable → close → exit (no Reenable).
- `--hold` keeps exclusive until SIGTERM (still no Reenable on exit).
- Path B needs a real non-lldb insert after **proven** p2 (see Path B design below).

## Dual-path (0.3.x)

| Path | Mechanism | When it works |
|------|-----------|---------------|
| **A direct** | Entitled `IOServiceOpen` type=1 + sel 3/4 | Client free |
| **A claim** | Boot LaunchDaemon: disable then sticky-release (or `--hold`) | Wins race before watchdogd |
| **B sock** | Unix socket to `libwwn_watchdogd_hook.dylib` | Hook loaded; live soft-inject fail-closed |
| Live soft-inject | `thread_set_state` / GOT | **Blocked** (Phase 1.4) |

### Proof gates

1. Path A claim — **FAIL without AMFI** (codesign). Lean entitlements in
   0.3.6; still needs `amfi_get_out_of_my_way=1` named reboot.
2. Path B `DYLD_INSERT` — **0.3.7 replacee** lab sticky OK; LaunchDaemon
   reboot proof still required. Arm: `claim-install --path-b` then reboot.
3. Take Over — **NOT FLIPPED**; `blocked-no-iowatchdog`.

### Path B reboot checklist (0.3.6 interpose; armed)

```text
1. Reboot (pathb plist staged; Apple watchdogd persist-disabled)
2. After login:
     cat /var/db/wwn-iowatchdog/claim-ok
     cat /tmp/libwayland-support/iowatchdog-userspace-disabled
     ls -la /var/run/wwn-iowatchdog.sock
     python3 -c "import socket;s=socket.socket(socket.AF_UNIX);s.connect('/var/run/wwn-iowatchdog.sock');s.send(b'ping\n');print(s.recv(64))"
     cat /var/log/wwn-iowatchdog-pathb.err.log
3. Hold 60s; no panic
4. Abort before reboot: sudo wwn-iowatchdog-claim-install --uninstall
```

### Path B load design (p3; after p2 only)

Goal: get `libwwn_watchdogd_hook.dylib` into watchdogd **without** lldb /
`thread_set_state` / GOT.

```text
A. Prove p2 sticky claim (marker held 60s, no panic).
B. Implement inject that actually sets DYLD_INSERT_LIBRARIES for the next
   watchdogd exec (SIP-off). Prefer a thin wrapper as ProgramArguments[0]
   that exports insert then execs /usr/libexec/watchdogd. Do not use
   kickstart -k. Do not rely on the current echo-only inject plist.
C. Reboot once with claim + insert staged (single controlled restart).
D. Confirm sock: ping / disable / enable round-trip ≥60s.
E. Hook must never crash (PanicOnConsecutiveCrash=true).
```

## Hook dylib

`libwwn_watchdogd_hook.dylib` (arm64e) is Path B. Load only after disable ACK
via boot-time insert (above); never via lldb.
