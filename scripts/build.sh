#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build"
realsense_source="/mnt/sdcard/librealsense"
realsense_build="${realsense_source}/build"

if [[ ! -f "${realsense_source}/include/librealsense2/rs.hpp" ]] ||
   [[ ! -f "${realsense_build}/librealsense2.so.2.50.0" ]]; then
    printf 'ERROR: expected pinned librealsense 2.50.0 source/build under %s\n' "${realsense_source}" >&2
    printf 'Mount /mnt/sdcard and verify the Hardware paths before building.\n' >&2
    exit 2
fi

cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DREALSENSE_SOURCE_DIR="${realsense_source}" \
    -DREALSENSE_BUILD_DIR="${realsense_build}" \
    -DRAKSH_BUILD_REALSENSE_SERVICE=ON \
    -DRAKSH_BUILD_TESTS=ON
cmake --build "${build_dir}" --parallel 2
ctest --test-dir "${build_dir}" --output-on-failure

printf 'Built: %s\n' "${build_dir}/raksh_depth_service"
