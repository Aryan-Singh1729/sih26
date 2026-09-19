#!/usr/bin/env bash
set -euo pipefail

base_url="${1:-http://127.0.0.1:8080}"
mapfile -t matches < <(
    for vendor_file in /sys/bus/usb/devices/*/idVendor; do
        device_dir="${vendor_file%/idVendor}"
        if [[ "$(<"${vendor_file}")" == "328f" ]] &&
           [[ -f "${device_dir}/idProduct" ]] &&
           [[ "$(<"${device_dir}/idProduct")" == "00ea" ]]; then
            basename "${device_dir}"
        fi
    done
)
if [[ "${#matches[@]}" -ne 1 ]]; then
    printf 'ERROR: expected exactly one 328f:00ea EMEET device, found %s\n' "${#matches[@]}" >&2
    exit 2
fi

usb_device="${matches[0]}"
needs_rebind=0
cleanup() {
    if [[ "${needs_rebind}" -eq 1 ]]; then
        printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

sudo -v
needs_rebind=1
printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null
sleep 3
health="$(curl -fsS "${base_url}/health")"
python3 -c '
import json, sys
h = json.loads(sys.argv[1])
assert h["service_state"] == "live", h
assert h["sr300_state"] == "live", h
assert h["emeet_state"] in ("disconnected", "stale"), h
print("PASS: EMEET disconnected while SR300 remained live", h)
' "${health}"

printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null
needs_rebind=0
printf 'EMEET rebound; restart the dashboard service to reopen the camera.\n'
