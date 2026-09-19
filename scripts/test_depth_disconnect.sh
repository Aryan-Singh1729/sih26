#!/usr/bin/env bash
set -euo pipefail

service="${1:-/tmp/raksh-depth-build/raksh_depth_service}"
serial="${RAKSH_SR300_SERIAL:-617205001375}"
log_file="/tmp/raksh-depth-disconnect-$(date -u +%Y%m%dT%H%M%SZ).log"
restart_log="${log_file%.log}-restart.log"

mapfile -t matches < <(
    for vendor_file in /sys/bus/usb/devices/*/idVendor; do
        device_dir="${vendor_file%/idVendor}"
        if [[ "$(<"${vendor_file}")" == "8086" ]] &&
           [[ -f "${device_dir}/idProduct" ]] &&
           [[ "$(<"${device_dir}/idProduct")" == "0aa5" ]]; then
            basename "${device_dir}"
        fi
    done
)

if [[ "${#matches[@]}" -ne 1 ]]; then
    printf 'ERROR: expected exactly one 8086:0aa5 device, found %s\n' "${#matches[@]}" >&2
    exit 2
fi
usb_device="${matches[0]}"

if [[ ! -x "${service}" ]]; then
    printf 'ERROR: service is not executable: %s\n' "${service}" >&2
    exit 2
fi

sudo -v
needs_rebind=0
service_pid=""
cleanup() {
    if [[ -n "${service_pid}" ]] && kill -0 "${service_pid}" 2>/dev/null; then
        kill -TERM "${service_pid}" 2>/dev/null || true
        wait "${service_pid}" 2>/dev/null || true
    fi
    if [[ "${needs_rebind}" -eq 1 ]]; then
        printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

"${service}" --serial "${serial}" --duration-seconds 60 --report-ms 500 \
    >"${log_file}" 2>&1 &
service_pid=$!
sleep 3

needs_rebind=1
printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/unbind >/dev/null

set +e
wait "${service_pid}"
service_status=$?
set -e
service_pid=""

printf '%s' "${usb_device}" | sudo tee /sys/bus/usb/drivers/usb/bind >/dev/null
needs_rebind=0
sleep 5

if [[ "${service_status}" -eq 0 ]]; then
    printf 'FAIL: service exited successfully after USB disconnect\n' >&2
    exit 3
fi
if ! grep -Eq 'state=(frame_timeout|runtime_error)' "${log_file}"; then
    printf 'FAIL: controlled timeout/runtime state missing; see %s\n' "${log_file}" >&2
    exit 4
fi

"${service}" --serial "${serial}" --duration-seconds 5 --report-ms 500 \
    >"${restart_log}" 2>&1
if ! grep -q 'state=live' "${restart_log}" ||
   ! grep -q 'state=stopped' "${restart_log}"; then
    printf 'FAIL: service did not recover after rebind; see %s\n' "${restart_log}" >&2
    exit 5
fi

printf 'PASS: disconnect produced controlled error and restart recovered\n'
printf '  usb_device=%s\n' "${usb_device}"
printf '  disconnect_exit_status=%s\n' "${service_status}"
printf '  disconnect_log=%s\n' "${log_file}"
printf '  restart_log=%s\n' "${restart_log}"
