#!/usr/bin/env bash
# Thin wrapper: prefer packaged unentitled claim-install next to this script
# or under a nix result path.
set -euo pipefail
ROOT="${1:-}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [[ -n "$ROOT" ]]; then
  HELPER="$ROOT/bin/wwn-iowatchdog-claim-install"
  BINDIR="$ROOT/bin"
elif [[ -x "$HERE/wwn-iowatchdog-claim-install" ]]; then
  HELPER="$HERE/wwn-iowatchdog-claim-install"
  BINDIR="$HERE"
else
  echo "usage: $0 [/path/to/wwn-iowatchdog/result]" >&2
  exit 2
fi
[[ -x "$HELPER" ]] || { echo "missing $HELPER" >&2; exit 3; }
exec "$HELPER" "$BINDIR"
