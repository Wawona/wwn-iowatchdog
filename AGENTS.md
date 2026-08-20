# wwn-iowatchdog agent notes

## What this repo is

L3′ **macOS Watchdog tools** for Wawona Desktop Mode B. First tool:
`src/wwn-iowatchdog.c`. Add more Watchdog CLIs here; do not grow Mode B
watchdog logic inside `Wawona/` sources.

## Never

- lldb attach to `watchdogd`
- unload / `kickstart -k` watchdogd without a successful disable
- add this flake as an input of L0-L2 (`wwn-toolchain`, `wwn-iland`, …)
- ship these binaries in App Store / Play / Apple-mobile artifacts

## DAG

See `Wawona/docs/wwn-repo-dag.md`. Consumer is Wawona only.
