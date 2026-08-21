#!/usr/bin/env bash
# Thin wrapper: Path A arm via unentitled claim-install --path-a.
set -euo pipefail
ROOT="${1:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ -n "$ROOT" ]]; then
  HELPER="$ROOT/bin/wwn-iowatchdog-claim-install"
elif [[ -x "$HERE/wwn-iowatchdog-claim-install" ]]; then
  HELPER="$HERE/wwn-iowatchdog-claim-install"
  ROOT="$(cd "$HERE/.." && pwd)"
else
  echo "usage: $0 [/path/to/wwn-iowatchdog/result]" >&2
  exit 2
fi
[[ -x "$HELPER" ]] || { echo "missing $HELPER" >&2; exit 3; }
exec "$HELPER" --path-a "$ROOT"
