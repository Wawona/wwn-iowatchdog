# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B. First tool:
`src/wwn-iowatchdog.c`. Add more Watchdog CLIs here; do not grow Mode B
watchdog logic inside `Wawona/` sources.

## Never

- lldb / debugserver / Cursor `lldb_mcp` attach to `watchdogd`
- unload / `kickstart -k` watchdogd without a successful disable
- open IOKit against IOWatchdog unless `WWN_IOWATCHDOG_ALLOW_OPEN=1`
- add this flake as an input of L0-L2 (`wwn-toolchain`, `wwn-iland`, …)
- ship these binaries in App Store / Play / Apple-mobile artifacts

Full policy (Take Over blocked, install defaults): Wawona
`docs/agent-rules/wawona-mode-b-watchdog-safety.md` and Cursor rule
`wawona-mode-b-watchdog-safety`.

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
