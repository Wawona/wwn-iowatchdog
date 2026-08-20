# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Home for privileged helpers that talk to kernel `IOWatchdog` / `watchdogd`.
Not graphics. Not Swinging Bridge. Never ship on iOS or in App Store / Play
artifacts.

## Tools

| Artifact | Purpose |
|----------|---------|
| `bin/wwn-iowatchdog` | `status` / `disable` / `enable` / `inject` |
| `lib/libwwn_watchdogd_hook.dylib` | arm64e hook for a future soft-inject path |

On macOS 26 (25F80), `disable`/`enable`/`inject` are **fail closed**: see
[docs/macos26-iowatchdog-wall.md](docs/macos26-iowatchdog-wall.md).
`status` can still locate the live `IOWatchdogUserClient` port name.

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
# disable / enable currently fail closed on macOS 26 (see docs).
```

## License

MIT. See [LICENSE](LICENSE).
