# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Home for privileged helpers that talk to kernel `IOWatchdog` / `watchdogd`.
Not graphics. Not Swinging Bridge. Never ship on iOS or in App Store / Play
artifacts.

## Tools

| Artifact | Purpose |
|----------|---------|
| `bin/wwn-iowatchdog` | `status` / `disable` / `enable` / `claim-install` / `claim-uninstall` / `inject-launchd` |
| `bin/wwn-iowatchdog-claim` | Opt-in daemon: open type=1, disable, hold exclusive |
| `lib/libwwn_watchdogd_hook.dylib` | arm64e Path B hook (Unix socket) |

### Dual path (0.3.0)

1. **Path A:** entitled direct `IOServiceOpen(IOWatchdog, type=1)` + selector
   3/4. Wins only when the client is free; use **claim** LaunchDaemon to win
   the boot race against `watchdogd`.
2. **Path B:** Unix socket to the hook inside `watchdogd` when loaded.
3. Live soft-inject (`thread_set_state` / GOT) stays **fail closed** on
   macOS 26 / 25F80. See [docs/macos26-iowatchdog-wall.md](docs/macos26-iowatchdog-wall.md).

Wawona Take Over stays `blocked-no-iowatchdog` until proof gates pass.

## Layer (repo DAG)

**L3′**. Depends on `nixpkgs` only. Consumers: **Wawona** (L4) desktop-host
Mode B only. See Wawona `docs/wwn-repo-dag.md`.

## Hard safety rules

1. **Never** lldb / debugserver / Cursor `lldb_mcp` on `watchdogd`.
2. **Never** unload / `kickstart -k` `com.apple.watchdogd` without a
   successful disable ACK.
3. Do not treat experimental inject as proven on 25F80.

## Build / run

```bash
nix build .#wwn-iowatchdog
sudo ./result/bin/wwn-iowatchdog status
# Opt-in claim (reboot to race watchdogd):
# sudo ./result/bin/wwn-iowatchdog claim-install
# sudo launchctl bootstrap system /Library/LaunchDaemons/com.aspauldingcode.wwn-iowatchdog-claim.plist
```

## License

MIT. See [LICENSE](LICENSE).
