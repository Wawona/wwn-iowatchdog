# macOS Watchdog tools

Canonical home: this repo (`wwn-iowatchdog`).

## Start here

| Doc | Role |
|-----|------|
| [`path-a-path-b.md`](path-a-path-b.md) | **How to run** Path A and Path B (arm, reboot proof, markers) |
| [`macos26-iowatchdog-wall.md`](macos26-iowatchdog-wall.md) | 25F80 investigation wall, RE, what stayed fail-closed |

Wawona Desktop Mode B must disable kernel IOWatchdog userspace monitoring
before unloading `com.apple.watchdogd`. Incident history (2026-08-19 /
2026-08-20 panics) lives in
`Wawona/docs/incident-reports/2026-08-19-windowserver-login/`.

Product prose: `Wawona/docs/iland-mode-a-b-desktop.md`.
Agent safety: `Wawona/docs/agent-rules/wawona-mode-b-watchdog-safety.md`.
