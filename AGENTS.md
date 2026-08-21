# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B (Path A entitled
open + claim; Path B hook socket). Lab scaffolding only on 25F80.

## Never

- lldb / debugserver / Cursor `lldb_mcp` attach to `watchdogd`
- unload / `kickstart -k` watchdogd without a successful disable ACK
- re-arm Path A / Path B without `WWN_IOW_PATHA_EXPERIMENT` /
  `WWN_IOW_PATHB_EXPERIMENT` (both **FAIL** on 25F80; see wall)
- flip Wawona Settings Take Over from this repo alone
- add this flake as an input of L0-L2
- ship these binaries in App Store / Play / Apple-mobile artifacts

## macOS 26 wall

**25F80 Classic hard wall (Phase 1.4 + Phase 2).** Soft-inject blocked.
Path A claim FAIL (`OS_REASON_CODESIGNING`). Path B `DYLD_INSERT` FAIL
(SIGBUS 138). Take Over stays `blocked-no-iowatchdog`. Full table:
`docs/macos26-iowatchdog-wall.md`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
