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

`thread_set_state` on watchdogd SIGKILLs the caller. IOKit port extract
and GOT patch fail. Take Over in Wawona stays blocked. Full table:
`docs/macos26-iowatchdog-wall.md`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
