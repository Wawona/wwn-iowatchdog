#!/usr/bin/env bash
# Thin wrapper around unentitled claim-install --uninstall.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ -x "$HERE/wwn-iowatchdog-claim-install" ]]; then
  exec "$HERE/wwn-iowatchdog-claim-install" --uninstall
fi
if [[ -n "${1:-}" && -x "$1/bin/wwn-iowatchdog-claim-install" ]]; then
  exec "$1/bin/wwn-iowatchdog-claim-install" --uninstall
fi
echo "usage: $0 [/path/to/wwn-iowatchdog/result]" >&2
echo "  or run from package bin/: wwn-iowatchdog-claim-install --uninstall" >&2
exit 2
