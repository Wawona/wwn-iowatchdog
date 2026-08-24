# Path A and Path B (`wwn-iowatchdog`)

How Wawona Desktop Mode B gets a sticky
`DisableUserspaceMonitoring` ACK on macOS 26 / 25F80 without lldb.

Canonical investigation / failure table:
[`macos26-iowatchdog-wall.md`](macos26-iowatchdog-wall.md).

## Why two paths

`IOWatchdog` type=1 is **exclusive**. Apple's `/usr/libexec/watchdogd`
normally holds the only client. You cannot open a second type=1 connection
while that holds (`0xe00002c5`).

Sticky disable (kext flag at `+0xa8`) survives closing the client. After a
successful Disable (sel **3**), you may close the connection and bring
Apple's `watchdogd` back; userspace monitoring stays off until Reenable
(sel **4**) or a full policy reset.

| | **Path B** (preferred) | **Path A** |
|---|---|---|
| Idea | Insert hook into Apple's `watchdogd`; call Disable on its live connection | Win the exclusive open yourself (entitled claim), Disable, close |
| Artifact | `libwwn_watchdogd_hook.dylib` (arm64e) + LaunchDaemon wrapper | `wwn-iowatchdog-claim` (ad-hoc `com.apple.private.iowatchdog.user-access`) |
| Installer | `claim-install --path-b` | `claim-install --path-a` |
| Extra boot-arg | None beyond lab SIP-off / `-arm64e_preview_abi` | **`amfi_get_out_of_my_way=1`** required |
| 25F80 status | **Reboot sticky proven** (0.3.7+) | **Reboot sticky proven** (0.3.9; AMFI + `outCnt=0`) |
| `pkg` arg | package root or `…/bin` | package root or `…/bin` |

Soft-inject / `thread_set_state` / lldb into live `watchdogd` stay
**fail closed** (panic class). Never use them.

## Shared markers

After a successful sticky Disable:

| Path | Meaning |
|------|---------|
| `/var/db/wwn-iowatchdog/claim-ok` | Durable ACK (`ok path=b …` or `ok path=a …`) |
| `/tmp/libwayland-support/iowatchdog-userspace-disabled` | Runtime marker (`path-b-auto` / `path-a-sticky` / …) |
| `/var/db/wwn-iowatchdog/claim-pending` | Armed but not yet ACK (cleared on success) |
| `/var/run/wwn-iowatchdog.sock` | Path B only: control socket inside hooked `watchdogd` |

Verify:

```bash
cat /var/db/wwn-iowatchdog/claim-ok
cat /tmp/libwayland-support/iowatchdog-userspace-disabled
# Path B:
ls -la /var/run/wwn-iowatchdog.sock
sudo python3 -c "import socket;s=socket.socket(socket.AF_UNIX);s.connect('/var/run/wwn-iowatchdog.sock');s.send(b'status\n');print(s.recv(200))"
```

Uninstall either path (restores Apple `com.apple.watchdogd`):

```bash
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --uninstall
```

Path A and Path B are **mutually exclusive** holders. Arming one tears down
the other's LaunchDaemon.

## Safety guards (0.3.10+; edge harden 0.3.11)

Invariants enforced in code at every Path A / Path B boundary. Not
docs-only tips.

**North star:** at every stable point, `/usr/libexec/watchdogd` is
**process-alive** (Apple job, or Path B wrapper exec of the same binary).
Never `kickstart -k`. Never leave Apple persist-disabled with no live
process and no staged successor that will run at next boot.

| API | When |
|-----|------|
| `wwn_safety_preflight_arm` | Before arm: root; no `DYLD_INSERT` in environ; live pid; AMFI for Path A; arm64e + codesign for Path B; refuse dual-path |
| Persist-disable | Only after plists/wrapper written; **verified** via `print-disabled`; only while a live pid still exists |
| `wwn_safety_postflight` | After arm / claim restore / uninstall: require live pid; else restore Apple and recheck; stamp `coverage-fail` on hard fail |
| `wwn_safety_reboot_successor_ok` | If Apple is persist-disabled, Path A or Path B plist must exist (else next boot is uncovered) |
| `wwn_watchdogd_job_restore` | enable; if down, re-register IOKit LaunchEvents (bootout+bootstrap of a not-running Apple job); dwell until `/usr/libexec/watchdogd` stays up; kickstart without `-k` only as last resort |
| Restore LaunchDaemon | Same dwell. A kickstart flash (exit 0, `KeepAlive SuccessfulExit=false`) is not coverage |
| Arm lock | `/var/db/wwn-iowatchdog/arm.lock` (flock) serializes arm/uninstall |

Doctor / verify (exit 0 only if covered **and** reboot-successor OK **and** not dual-path):

```bash
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --doctor
# synonym:
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --verify
# orphan disable / dual-path / uncovered:
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --heal
```

Hard refuses (non-zero, no partial arm):

- Arm with no live `watchdogd` already running
- Path A without `amfi_get_out_of_my_way=1` unless `WWN_IOW_PATHA_FORCE=1`
- Path B if hook missing, not arm64e, or `codesign -v` fails
- Concurrent arm/uninstall (lock busy)
- Uninstall/heal that cannot restore a live Apple `watchdogd`
- Postflight still uncovered after restore attempt
- Doctor: orphan persist-disable (Apple disabled, no Path A/B plist)
- Doctor: both Path A and Path B plists present

Edge cases covered in 0.3.11:

- Partial arm abort cleans staged plists + pending
- Uninstall enables Apple **before** bootout of Path B (shortens uncovered window)
- `nvram` boot-args escaping for quotes in existing args
- Stale `claim-held` pid reported by doctor
- Build refuses `launchctl kickstart -k` as a command string in `src/`

## Build once

```bash
nix build /path/to/wwn-iowatchdog#wwn-iowatchdog --out-link /tmp/wwn-iow
# or:
nix run /path/to/wwn-iowatchdog#claim-install -- --help
```

## Path B (DYLD_INSERT + interpose)

### Mechanism

1. Persist-disable Apple `com.apple.watchdogd`.
2. Stage `libwwn_watchdogd_hook.dylib` under
   `/Library/Application Support/Wawona/wwn-iowatchdog/`.
3. LaunchDaemon `com.aspauldingcode.wwn-iowatchdog-pathb` runs a wrapper that
   **prefixes** `DYLD_INSERT_LIBRARIES` on the `exec` (via `/usr/bin/env`);
   it never `export`s that variable into a parent shell.
4. Hook uses dyld `__DATA,__interpose` on `IOConnectCallScalarMethod`.
5. **Critical (0.3.7+):** call the real function via the interpose
   **replacee** pointer. `dlsym` / `dlopen(IOKit)` returns the hook itself
   and recurses (SIGSEGV).
6. Auto-disable: next live scalar call after arm is rewritten to sel 3
   (Disable), using the caller's buffers. Reporter thread writes markers
   (no FILE I/O inside the hook hot path).
7. Unix socket `/var/run/wwn-iowatchdog.sock`: `ping` / `status` /
   `disable` (arms next-call rewrite) / `enable`.

### Arm + reboot proof

```bash
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-b /tmp/wwn-iow
# Reboot. Do not leave the machine with Apple disabled and pathb unstarted
# across a long session without rebooting.
```

After login, expect:

```text
claim-ok: ok path=b sticky=1 replacee=1
marker:   path-b-auto
sock:     done=1 dkr=0x0
launchctl: system/com.aspauldingcode.wwn-iowatchdog-pathb state=running
Apple:    com.apple.watchdogd => disabled
```

### Known pitfalls

- fishhook GOT patch on arm64e → SIGBUS 138 (removed).
- `dlsym(RTLD_NEXT)` as “original” → infinite recursion.
- FILE I/O or `getenv` inside the interposed call → instability / SEGV.
- Synthetic Disable with `NULL` output buffers → `0xe00002c2`; use live
  caller buffers or a next-call rewrite.
- Never `export DYLD_INSERT_LIBRARIES` in a shell that later runs
  `launchctl` (pollutes children). Prefix the env on the `watchdogd`
  command only (the wrapper does this).

## Path A (entitled claim)

### Mechanism

1. Persist-disable Apple `com.apple.watchdogd` so exclusive type=1 is free
   at boot.
2. LaunchDaemon runs `wwn-iowatchdog-claim` (codesigned with only
   `com.apple.private.iowatchdog.user-access`).
3. Claim opens IOWatchdog type=1, calls Disable (sel 3) with scalar out
   buffers, writes markers, closes (sticky-after-close), then restores
   Apple `watchdogd`.
4. Optional `--hold`: keep exclusive until SIGTERM.

### AMFI requirement

Ad-hoc private entitlements are killed by AMFI on 25F80
(`OS_REASON_CODESIGNING` / exit 137) unless:

```text
amfi_get_out_of_my_way=1
```

is in `kern.bootargs`. SIP-off alone is not enough.

```bash
# Append boot-arg (preserves existing args such as -arm64e_preview_abi)
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a-amfi-nvram
# Reboot so the boot-arg is live
```

Installer refuses `--path-a` without that boot-arg unless
`WWN_IOW_PATHA_FORCE=1` (expect 137).

### Arm + reboot proof

```bash
# After AMFI boot-arg is active:
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a /tmp/wwn-iow
# Reboot for claim RunAtLoad
```

After login, expect:

```text
claim-ok: ok path=a sticky=1 time=…
marker:   path-a-sticky
Apple:    com.apple.watchdogd re-enabled after claim (sticky remains)
```

Revert AMFI when done:

```bash
# Example if you only had -arm64e_preview_abi before:
sudo nvram 'boot-args=-arm64e_preview_abi'
# reboot
```

### Known pitfalls

- Boot race without persist-disable: Apple wins exclusive → claim open fails.
- Interactive entitled CLI without AMFI → often dies before `main`.
- Disable with `outputCnt > 0` → `0xe00002c2` on 25F80. Use `outCnt=0` and
  `NULL` output (claim 0.3.9+).
- Prefer Path B on the daily driver; Path A AMFI-off is lab-shaped.

## Safety (both paths)

1. Never lldb / `lldb_mcp` / debugserver on `watchdogd`.
2. Never `launchctl kickstart -k` Apple `com.apple.watchdogd` without a
   disable ACK.
3. Never unload `watchdogd` while kernel IOWatchdog userspace monitoring
   is still armed (panic: `watchdogd[pid] exited`).
4. Take Over product wiring consumes these ACKs separately; do not flip
   Settings Take Over until the product gate is updated on purpose.

## CLI quick reference

```bash
# Installer (unentitled; safe to run interactively)
wwn-iowatchdog-claim-install --help
wwn-iowatchdog-claim-install --path-b /tmp/wwn-iow
wwn-iowatchdog-claim-install --path-a-amfi-nvram
wwn-iowatchdog-claim-install --path-a /tmp/wwn-iow
wwn-iowatchdog-claim-install --uninstall

# Path A claim binary (entitled; AMFI-sensitive)
wwn-iowatchdog-claim [--hold]

# Status / sock helpers (may be AMFI-sensitive if entitled)
wwn-iowatchdog status
```

## Version map

| Version | Change |
|---------|--------|
| 0.3.6 | Path B: drop fishhook; `DYLD_INTERPOSE` |
| 0.3.7 | Path B: call interpose **replacee**; reboot sticky proven |
| 0.3.8 | Path A: `--path-a`, `--path-a-amfi-nvram`, flake `claim-install` app |
| 0.3.9 | Path A: Disable `outCnt=0`; pkg `bin/` resolve; stage-only arm; Apple restore **bootstrap** |
