#!/bin/zsh

set -euo pipefail

profile="${1:-coop-skip}"
primary_player="${2:-dryxio}"
secondary_player="${3:-EvasivePanpipe6_CL2}"
timeout_seconds="${4:-1200}"
vm_name="${TAGUP_VM_NAME:-Windows 11}"
runner='C:\Mac\Home\Documents\GitHub\mtasa-neon\utils\tagging-up-turf-transition-harness.ps1'

case "$profile" in
    solo-natural|solo-skip|coop-natural|coop-skip|solo-native-natural|solo-native-skip|coop-native-natural|coop-native-skip) ;;
    *)
        print -u2 -- "usage: $0 [solo-natural|solo-skip|coop-natural|coop-skip|solo-native-natural|solo-native-skip|coop-native-natural|coop-native-skip] [primary-player] [secondary-player] [timeout-seconds]"
        exit 64
        ;;
esac

prlctl exec "$vm_name" --current-user powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$runner" \
    -Profile "$profile" \
    -PrimaryPlayer "$primary_player" \
    -SecondaryPlayer "$secondary_player" \
    -TimeoutSeconds "$timeout_seconds"
