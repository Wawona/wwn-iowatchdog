# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B (Path A entitled
open + claim-hold; Path B hook socket).

## Never

- lldb / debugserver / Cursor `lldb_mcp` attach to `watchdogd`
- unload / `kickstart -k` watchdogd without a successful disable ACK
- claim disable works on macOS 26 without re-proving against
  `docs/macos26-iowatchdog-wall.md`
- flip Wawona Settings Take Over from this repo alone
- add this flake as an input of L0-L2
- ship these binaries in App Store / Play / Apple-mobile artifacts

## macOS 26 wall

**25F80 Classic blocked (Phase 1.4) for live soft-inject.** Dual-path
scaffolding is in tree: Path A works when the client is free / claim wins
boot; Path B needs the hook loaded. Take Over in Wawona stays
`blocked-no-iowatchdog` until proof gates in the wall doc. Full table:
`docs/macos26-iowatchdog-wall.md`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
