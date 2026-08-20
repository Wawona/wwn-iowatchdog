# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B.

## Never

- lldb / debugserver / Cursor `lldb_mcp` attach to `watchdogd`
- unload / `kickstart -k` watchdogd without a successful disable ACK
- claim disable works on macOS 26 without re-proving against
  `docs/macos26-iowatchdog-wall.md`
- add this flake as an input of L0-L2
- ship these binaries in App Store / Play / Apple-mobile artifacts

## macOS 26 wall

**25F80 Classic blocked (Phase 1.4).** `thread_set_state` is panic-class.
IOKit extract fails. AMFI boot-arg did not unlock a proven
`DisableUserspaceMonitoring` path. `disable`/`enable`/`inject` stay fail
closed. Take Over in Wawona stays `blocked-no-iowatchdog`. Full table:
`docs/macos26-iowatchdog-wall.md`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
