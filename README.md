# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Home for privileged helpers that talk to kernel `IOWatchdog` / `watchdogd`.
Not graphics. Not Swinging Bridge. Never ship on iOS or in App Store / Play
artifacts.

## Tools

| Tool | Purpose |
|------|---------|
| `wwn-iowatchdog` | `status` / `disable` / `enable` userspace IOWatchdog monitoring so Mode B may unload `com.apple.watchdogd` without an immediate XNU panic |

More Watchdog-related CLIs belong in this repo (not in `Wawona/` or
`wwn-iland`).

## Layer (repo DAG)

**L3′**. Depends on `nixpkgs` only (no `wwn-toolchain` / `wwn-iland`).
Consumers: **Wawona** (L4) bundles the binary into desktop-host Mode B
(`Contents/Library/Wawona/wwn-iowatchdog`). See
[`Wawona/docs/wwn-repo-dag.md`](https://github.com/Wawona/Wawona/blob/development/docs/wwn-repo-dag.md).

## Hard safety rules (macOS 26 / 25F80)

1. **Never** `lldb` / debugserver attach to `watchdogd` (including Cursor
   `lldb_mcp`). Exit reason namespace 2 / subcode 0x5 (SIGTRAP) while kernel
   monitoring is armed panics the box (`watchdogd[pid] exited`).
2. **Never** unload / `kickstart -k` `com.apple.watchdogd` unless
   `wwn-iowatchdog disable` has already succeeded via a proven exclusive
   open.
3. Default CLI is **fail closed**: no `IOServiceOpen`, no `lsmp` against
   `watchdogd`. Set `WWN_IOWATCHDOG_ALLOW_OPEN=1` only for experiments.
4. Stage / install / blocked Take Over must not probe disable/enable or
   install `ws-guard` until KEEP_WS probe inject.

## Build / run

```bash
nix build .#wwn-iowatchdog
sudo ./result/bin/wwn-iowatchdog status
# disable / enable require root and a working exclusive open (often blocked
# while watchdogd holds IOWatchdogUserClient).
```

## License

MIT. See [LICENSE](LICENSE).
