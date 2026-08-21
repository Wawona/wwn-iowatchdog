# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B (Path A entitled
claim; Path B arm64e DYLD_INTERPOSE hook). Soft-inject stays fail-closed.

## Never

- lldb / debugserver / Cursor `lldb_mcp` attach to `watchdogd`
- `launchctl kickstart -k` on Apple `com.apple.watchdogd`
- leave Apple persist-disabled with no live process and no Path A/B
  successor plist (use `--doctor` / `--heal`)
- `export DYLD_INSERT_LIBRARIES` in a parent shell (prefix on Path B
  wrapper `exec` only)
- flip Wawona Settings Take Over from this repo alone (product gate is
  separate; stays `blocked-no-iowatchdog` until product wires ACKs)
- add this flake as an input of L0-L2
- ship these binaries in App Store / Play / Apple-mobile artifacts

## Status (25F80)

- Path B reboot sticky: **proven** (0.3.7+)
- Path A reboot sticky: **proven** (0.3.9; needs `amfi_get_out_of_my_way=1`)
- Soft-inject / `thread_set_state`: **fail closed**
- Safety guards: `--doctor` / `--heal` (0.3.10+; edge harden 0.3.11; stale Path B sock 0.3.13)

Operator guide: `docs/path-a-path-b.md`. Wall table:
`docs/macos26-iowatchdog-wall.md`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
