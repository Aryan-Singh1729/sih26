#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
service="${1:-${project_dir}/build/raksh_depth_service}"
duration_seconds="${RAKSH_VALIDATION_SECONDS:-900}"
serial="${RAKSH_SR300_SERIAL:-617205001375}"
run_id="$(date -u +%Y%m%dT%H%M%SZ)"
log_file="/tmp/raksh-depth-${run_id}.log"
memory_file="/tmp/raksh-depth-${run_id}-rss-kib.log"

if [[ ! -x "${service}" ]]; then
    printf 'ERROR: service is not executable: %s\n' "${service}" >&2
    exit 2
fi

cleanup() {
    if [[ -n "${service_pid:-}" ]] && kill -0 "${service_pid}" 2>/dev/null; then
        kill -TERM "${service_pid}" 2>/dev/null || true
        wait "${service_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

"${service}" --serial "${serial}" --duration-seconds "${duration_seconds}" \
    --report-ms 1000 >"${log_file}" 2>&1 &
service_pid=$!

while kill -0 "${service_pid}" 2>/dev/null; do
    if [[ -r "/proc/${service_pid}/status" ]]; then
        awk -v now="$(date -u +%FT%TZ)" \
            '$1 == "VmRSS:" { print now, $2 }' \
            "/proc/${service_pid}/status" >>"${memory_file}"
    fi
    sleep 10
done

set +e
wait "${service_pid}"
service_status=$?
set -e
service_pid=""

if [[ "${service_status}" -ne 0 ]]; then
    printf 'FAIL: service exited %s; see %s\n' "${service_status}" "${log_file}" >&2
    tail -n 20 "${log_file}" >&2
    exit "${service_status}"
fi

if grep -Eq 'state=(camera_missing|start_failed|frame_timeout|runtime_error|stale)' "${log_file}"; then
    printf 'FAIL: unhealthy acquisition state found; see %s\n' "${log_file}" >&2
    exit 3
fi

if ! grep -q 'state=stopped' "${log_file}"; then
    printf 'FAIL: orderly stopped state was not recorded; see %s\n' "${log_file}" >&2
    exit 4
fi

average_fps="$(awk '
    {
        for (field = 1; field <= NF; field++) {
            if ($field ~ /^fps=/) {
                value = $field
                sub(/^fps=/, "", value)
                total += value
                count++
            }
        }
    }
    END { if (count == 0) print 0; else printf "%.2f", total / count }
' "${log_file}")"

read -r minimum_rss maximum_rss < <(
    awk 'NR > 6 { if (minimum == 0 || $2 < minimum) minimum=$2; if ($2 > maximum) maximum=$2 }
         END { print minimum+0, maximum+0 }' "${memory_file}"
)
rss_span_kib=$((maximum_rss - minimum_rss))

printf 'PASS: live run completed\n'
printf '  duration_seconds=%s\n' "${duration_seconds}"
printf '  average_reported_fps=%s\n' "${average_fps}"
printf '  post_warmup_rss_min_kib=%s\n' "${minimum_rss}"
printf '  post_warmup_rss_max_kib=%s\n' "${maximum_rss}"
printf '  post_warmup_rss_span_kib=%s\n' "${rss_span_kib}"
printf '  diagnostics=%s\n' "${log_file}"
printf '  memory_samples=%s\n' "${memory_file}"

awk -v fps="${average_fps}" 'BEGIN { exit !(fps >= 27.0 && fps <= 33.0) }' || {
    printf 'FAIL: average reported FPS is outside 27-33 FPS\n' >&2
    exit 5
}

if (( rss_span_kib > 12288 )); then
    printf 'FAIL: post-warmup RSS span exceeds 12 MiB\n' >&2
    exit 6
fi
