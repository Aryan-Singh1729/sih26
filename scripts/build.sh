#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build"
realsense_config="/mnt/sdcard/librealsense/build"

if [[ ! -f "${realsense_config}/realsense2Config.cmake" ]]; then
    printf 'ERROR: expected librealsense CMake config at %s\n' "${realsense_config}/realsense2Config.cmake" >&2
    printf 'Mount /mnt/sdcard and verify the Hardware paths before building.\n' >&2
    exit 2
fi

cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -Drealsense2_DIR="${realsense_config}" \
    -DRAKSH_BUILD_REALSENSE_SERVICE=ON \
    -DRAKSH_BUILD_TESTS=ON
cmake --build "${build_dir}" --parallel 2
ctest --test-dir "${build_dir}" --output-on-failure

printf 'Built: %s\n' "${build_dir}/raksh_depth_service"
