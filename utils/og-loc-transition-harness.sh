#!/bin/zsh

set -euo pipefail

vm_name="${OGL_VM_NAME:-Windows 11}"
secondary_player="${1:-EvasivePanpipe6_CL2}"
timeout_seconds="${2:-900}"
runner='C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\og-loc-transition-harness.ps1'

runner_args=(-File "$runner" -TimeoutSeconds "$timeout_seconds")
if [[ "$secondary_player" != "-" ]]; then
    runner_args+=(-SecondaryPlayer "$secondary_player")
fi

set +e
prlctl exec "$vm_name" --current-user powershell.exe -NoProfile -ExecutionPolicy Bypass \
    "${runner_args[@]}" 2>&1 |
while IFS= read -r line; do
    print -r -- "$line"
    if [[ "$line" == *"awaiting-ui-W"* ]]; then
        # The Windows runner has focused the exact GTA process named by this
        # line. Send the matching VM keyboard event through Parallels so MTA's
        # ordinary onClientKey path and GTA's raw control state are both real.
        prlctl send-key-event "$vm_name" --scancode 17 --event press >/dev/null
        osascript -e 'tell application "Parallels Desktop" to activate' \
            -e 'delay 0.1' \
            -e 'tell application "System Events" to keystroke "w"'
        sleep 1
        prlctl send-key-event "$vm_name" --scancode 17 --event release >/dev/null
    fi
done
runner_status=${pipestatus[1]}
set -e
exit "$runner_status"
