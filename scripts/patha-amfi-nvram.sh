#!/usr/bin/env bash
# Append amfi_get_out_of_my_way=1 to nvram boot-args (SIP-off lab only).
# Required for Path A ad-hoc com.apple.private.iowatchdog.user-access.
set -euo pipefail
if [[ "${EUID}" -ne 0 ]]; then
  echo "needs root" >&2
  exit 1
fi
cur="$(sysctl -n kern.bootargs 2>/dev/null || true)"
if [[ "$cur" == *amfi_get_out_of_my_way=1* ]]; then
  echo "already present: $cur"
  exit 0
fi
# Preserve existing args (e.g. -arm64e_preview_abi).
new="${cur:+$cur }amfi_get_out_of_my_way=1"
nvram "boot-args=${new}"
echo "nvram boot-args set to: $(nvram boot-args 2>/dev/null | cut -f2-)"
echo "Reboot required before Path A claim will survive AMFI."
echo "Revert later: sudo nvram boot-args='${cur}'"
