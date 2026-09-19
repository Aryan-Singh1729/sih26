#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
config_file="${1:-${project_dir}/config/raksh-dashboard.conf}"
if [[ ! -f "${config_file}" ]]; then
    config_file="${project_dir}/config/raksh-dashboard.conf.example"
    printf 'WARNING: using uncalibrated example configuration: %s\n' "${config_file}" >&2
fi
# shellcheck source=/dev/null
source "${config_file}"

binary="${RAKSH_DASHBOARD_BINARY:-${project_dir}/build/raksh_dashboard_server}"
web_root="${project_dir}/${WEB_ROOT}"
log_dir="${LOG_DIRECTORY:-/tmp/raksh-dashboard}"
log_file="${log_dir}/dashboard.log"

fail() { printf 'PREFLIGHT FAILED: %s\n' "$*" >&2; exit 2; }
[[ -x "${binary}" ]] || fail "missing executable ${binary}; run scripts/build.sh"
findmnt -rn /mnt/sdcard >/dev/null || fail "/mnt/sdcard is not mounted"
[[ -f /mnt/sdcard/librealsense/build/librealsense2.so.2.50.0 ]] ||
    fail "librealsense v2.50.0 library is missing"
lsusb -d 8086:0aa5 >/dev/null || fail "SR300 USB device 8086:0aa5 is missing"
lsusb -d 328f:00ea >/dev/null || fail "EMEET USB device 328f:00ea is missing"
[[ -e "${EMEET_DEVICE}" ]] || fail "stable EMEET by-id path is missing"
[[ -f "${web_root}/index.html" && -f "${web_root}/dashboard.js" &&
   -f "${web_root}/dashboard.css" ]] || fail "dashboard web assets are missing"
if ss -H -ltn | awk '{print $4}' | grep -Eq "[:.]${PORT}$"; then
    fail "TCP port ${PORT} is already in use"
fi

mkdir -p "${log_dir}"
if [[ -f "${log_file}" ]] &&
   [[ "$(stat -c %s "${log_file}")" -ge "${LOG_MAX_BYTES:-524288}" ]]; then
    for ((index=${LOG_KEEP:-3}; index>=2; --index)); do
        previous=$((index - 1))
        [[ -f "${log_file}.${previous}" ]] &&
            mv -f "${log_file}.${previous}" "${log_file}.${index}"
    done
    mv -f "${log_file}" "${log_file}.1"
fi

printf 'Starting Raksh dashboard on %s:%s (log %s)\n' \
    "${BIND_ADDRESS}" "${PORT}" "${log_file}"
set +e
"${binary}" \
  --serial "${SR300_SERIAL}" \
  --bind "${BIND_ADDRESS}" --port "${PORT}" --web-root "${web_root}" \
  --camera-device "${EMEET_DEVICE}" --camera-width "${EMEET_WIDTH}" \
  --camera-height "${EMEET_HEIGHT}" --camera-fps "${EMEET_FPS}" \
  --camera-height-m "${CAMERA_HEIGHT_M}" --camera-pitch-deg "${CAMERA_PITCH_DEG}" \
  --minimum-distance-m "${MINIMUM_DISTANCE_M}" --maximum-distance-m "${MAXIMUM_DISTANCE_M}" \
  --floor-tolerance-m "${FLOOR_TOLERANCE_M}" \
  --risk-critical-m "${RISK_CRITICAL_M}" --risk-close-m "${RISK_CLOSE_M}" \
  --risk-intermediate-m "${RISK_INTERMEDIATE_M}" 2>&1 | tee -a "${log_file}"
status=${PIPESTATUS[0]}
set -e
exit "${status}"
