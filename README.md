# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Home for privileged helpers that talk to kernel `IOWatchdog` / `watchdogd`.
Not graphics. Not Swinging Bridge. Never ship on iOS or in App Store / Play
artifacts.

## Tools

| Artifact | Purpose |
|----------|---------|
| `bin/wwn-iowatchdog` | `status` / `disable` / `enable` / `inject-launchd` (entitled; may SIGKILL interactively) |
| `bin/wwn-iowatchdog-claim-install` | Unentitled install/uninstall (Path A/B **refused by default** on 25F80) |
| `bin/wwn-iowatchdog-claim` | Boot claim daemon (lab only behind `WWN_IOW_PATHA_EXPERIMENT`) |
| `lib/libwwn_watchdogd_hook.dylib` | arm64e Path B hook (lab only; insert SIGBUS on 25F80) |

### Dual path (0.3.5) — 25F80 hard wall

1. **Path A:** entitled `IOServiceOpen` + sel 3/4. Claim reboot **FAIL**
   (`OS_REASON_CODESIGNING`). Arm only with `WWN_IOW_PATHA_EXPERIMENT=1`.
2. **Path B:** `DYLD_INSERT` hook into `watchdogd`. Reboot **FAIL** (SIGBUS
   138). Arm only with `WWN_IOW_PATHB_EXPERIMENT=1`.
3. Live soft-inject stays **fail closed**. See
   [docs/macos26-iowatchdog-wall.md](docs/macos26-iowatchdog-wall.md).

Wawona Take Over stays `blocked-no-iowatchdog`.

## Layer (repo DAG)

**L3′**. Depends on `nixpkgs` only. Consumers: **Wawona** (L4) desktop-host
Mode B only. See Wawona `docs/wwn-repo-dag.md`.

## Hard safety rules

1. **Never** lldb / debugserver / Cursor `lldb_mcp` on `watchdogd`.
2. **Never** unload / `kickstart -k` `com.apple.watchdogd` without a
   successful disable ACK.
3. Do not treat experimental inject as proven on 25F80.
4. Do not re-arm claim / Path B on the daily driver without a named experiment.

## Build / run

```bash
nix build .#wwn-iowatchdog
sudo ./result/bin/wwn-iowatchdog status   # may SIGKILL 137 interactively
# Clean any prior arm:
sudo ./result/bin/wwn-iowatchdog-claim-install --uninstall
```

## License

MIT. See [LICENSE](LICENSE).
