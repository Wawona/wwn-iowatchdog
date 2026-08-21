# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Talks to kernel `IOWatchdog` / `watchdogd`. Not graphics. Not Swinging
Bridge. Never ship on iOS or in App Store / Play artifacts.

## Path A vs Path B

Full operator guide: **[`docs/path-a-path-b.md`](docs/path-a-path-b.md)**.

| Path | What it does | 25F80 |
|------|----------------|-------|
| **B** (preferred) | `DYLD_INSERT` hook into Apple's `watchdogd`; Disable on its connection | Reboot sticky **proven** |
| **A** | Entitled claim opens type=1, Disable, close (sticky) | Needs `amfi_get_out_of_my_way=1` |

Investigation wall (failures, RE): [`docs/macos26-iowatchdog-wall.md`](docs/macos26-iowatchdog-wall.md).

### Quick arm

```bash
nix build .#wwn-iowatchdog --out-link /tmp/wwn-iow

# Path B (no AMFI boot-arg)
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-b /tmp/wwn-iow
# reboot → cat /var/db/wwn-iowatchdog/claim-ok

# Path A (lab AMFI-off)
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a-amfi-nvram
# reboot, then:
sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a /tmp/wwn-iow
# reboot → cat /var/db/wwn-iowatchdog/claim-ok

sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --uninstall
```

`nix run .#claim-install -- --help` works.

## Tools

| Artifact | Purpose |
|----------|---------|
| `bin/wwn-iowatchdog` | `status` / `disable` / `enable` (direct or Path B sock) |
| `bin/wwn-iowatchdog-claim-install` | `--path-a` / `--path-b` / `--path-a-amfi-nvram` / `--uninstall` |
| `bin/wwn-iowatchdog-claim` | Path A boot claim (entitled) |
| `lib/libwwn_watchdogd_hook.dylib` | arm64e Path B interpose hook |

## Layer (repo DAG)

**L3′**. Depends on `nixpkgs` only. Consumers: **Wawona** (L4) desktop-host
Mode B only. See Wawona `docs/wwn-repo-dag.md`.

## Hard safety rules

1. **Never** lldb / debugserver / Cursor `lldb_mcp` on `watchdogd`.
2. **Never** unload / `kickstart -k` `com.apple.watchdogd` without a
   successful disable ACK.
3. Soft-inject / `thread_set_state` stay fail closed on 25F80.
4. Path A AMFI-off is lab-only; prefer Path B on the daily driver.

## License

MIT. See [LICENSE](LICENSE).
