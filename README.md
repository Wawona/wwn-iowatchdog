# wwn-iowatchdog

macOS **Watchdog** tools for Wawona Desktop / LockScreen Mode B.

Home for privileged helpers that talk to kernel `IOWatchdog` / `watchdogd`.
Not graphics. Not Swinging Bridge. Never ship on iOS or in App Store / Play
artifacts.

## Tools

| Artifact | Purpose |
|----------|---------|
| `bin/wwn-iowatchdog` | `status` / `disable` / `enable` (Path A direct or Path B sock) |
| `bin/wwn-iowatchdog-claim-install` | Unentitled `--path-a` / `--path-b` / `--uninstall` |
| `bin/wwn-iowatchdog-claim` | Path A boot claim (entitled; needs AMFI relaxed) |
| `lib/libwwn_watchdogd_hook.dylib` | arm64e Path B `DYLD_INTERPOSE` hook |

### Dual path (0.3.8)

1. **Path B (preferred on 25F80):** `DYLD_INSERT` + interpose **replacee**.
   Reboot sticky **proven** (`claim-ok` / `dkr=0x0`).
   ```bash
   nix build .#wwn-iowatchdog --out-link /tmp/wwn-iow
   sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-b /tmp/wwn-iow
   # reboot, then cat /var/db/wwn-iowatchdog/claim-ok
   ```
2. **Path A:** entitled `IOServiceOpen` + sel 3. Needs
   `amfi_get_out_of_my_way=1` or AMFI kills ad-hoc
   `com.apple.private.iowatchdog.user-access` (`OS_REASON_CODESIGNING` / 137).
   ```bash
   sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a-amfi-nvram
   # reboot
   sudo /tmp/wwn-iow/bin/wwn-iowatchdog-claim-install --path-a /tmp/wwn-iow
   # reboot again for claim RunAtLoad
   ```
3. Live soft-inject stays **fail closed**. See
   [docs/macos26-iowatchdog-wall.md](docs/macos26-iowatchdog-wall.md).

`nix run .#claim-install -- --help` works (flake app).

## Layer (repo DAG)

**L3′**. Depends on `nixpkgs` only. Consumers: **Wawona** (L4) desktop-host
Mode B only. See Wawona `docs/wwn-repo-dag.md`.

## Hard safety rules

1. **Never** lldb / debugserver / Cursor `lldb_mcp` on `watchdogd`.
2. **Never** unload / `kickstart -k` `com.apple.watchdogd` without a
   successful disable ACK.
3. Do not treat experimental inject as proven on 25F80.
4. Path A AMFI-off is lab-only; prefer Path B on the daily driver.

## Build / run

```bash
nix build .#wwn-iowatchdog
sudo ./result/bin/wwn-iowatchdog-claim-install --uninstall
```

## License

MIT. See [LICENSE](LICENSE).
