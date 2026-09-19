#!/usr/bin/env bash

set -u

SD_MOUNT="${RAKSH_SD_MOUNT:-/mnt/sdcard}"
REALSENSE_BUILD="${RAKSH_REALSENSE_BUILD:-${SD_MOUNT}/librealsense/build}"
ENUMERATE="${REALSENSE_BUILD}/tools/enumerate-devices/rs-enumerate-devices"

section() {
  printf '\n=== %s ===\n' "$1"
}

section "SYSTEM"
date --iso-8601=seconds
hostname
uname -a
printf 'architecture='
dpkg --print-architecture
grep -E '^(PRETTY_NAME|VERSION_ID|DEBIAN_VERSION_FULL)=' /etc/os-release
free -h

section "STORAGE"
if findmnt --mountpoint "$SD_MOUNT"; then
  df -hT "$SD_MOUNT"
else
  printf 'ERROR: %s is not a mount point\n' "$SD_MOUNT" >&2
fi
lsblk -o NAME,PATH,SIZE,FSTYPE,LABEL,UUID,MOUNTPOINTS,MODEL,TRAN

section "USB"
lsusb
lsusb -t

section "VIDEO DEVICES"
v4l2-ctl --list-devices
find /dev/v4l/by-id -maxdepth 1 -type l -printf '%f -> %l\n' 2>/dev/null | sort

section "EMEET"
EMEET_DEVICE="$(find /dev/v4l/by-id -maxdepth 1 -type l \
  -name '*EMEET*video-index0' -print -quit 2>/dev/null)"
if [[ -n "$EMEET_DEVICE" ]]; then
  printf 'device_by_id=%s\n' "$EMEET_DEVICE"
  printf 'resolved_device=%s\n' "$(readlink -f "$EMEET_DEVICE")"
  udevadm info --query=property --name="$EMEET_DEVICE" \
    | grep -E '^(ID_VENDOR_ID|ID_MODEL_ID|ID_VENDOR=|ID_MODEL=|ID_SERIAL=|ID_V4L_PRODUCT=|ID_PATH=)'
  v4l2-ctl --device="$EMEET_DEVICE" --list-formats-ext
else
  printf 'ERROR: stable EMEET video-index0 link not found\n' >&2
fi

section "REALSENSE"
if [[ -x "$ENUMERATE" ]]; then
  git -C "${SD_MOUNT}/librealsense" describe --tags --always --dirty
  "$ENUMERATE" --version
  "$ENUMERATE"
else
  printf 'ERROR: rs-enumerate-devices not found at %s\n' "$ENUMERATE" >&2
fi

section "BUILD DEPENDENCIES"
for tool in g++ gcc cmake make pkg-config v4l2-ctl ffmpeg ffprobe git; do
  if command -v "$tool" >/dev/null 2>&1; then
    case "$tool" in
      ffmpeg|ffprobe) "$tool" -version 2>/dev/null | head -n 1 ;;
      *) "$tool" --version 2>/dev/null | head -n 1 ;;
    esac
  else
    printf '%s: MISSING\n' "$tool"
  fi
done

if [[ -f "${REALSENSE_BUILD}/realsense2Config.cmake" ]]; then
  printf 'realsense_cmake_config=present\n'
else
  printf 'realsense_cmake_config=missing\n'
fi

[[ -f /usr/include/linux/videodev2.h ]] \
  && printf 'v4l2_headers=present\n' \
  || printf 'v4l2_headers=missing\n'
[[ -f /usr/include/boost/beast.hpp ]] \
  && printf 'boost_beast_headers=present\n' \
  || printf 'boost_beast_headers=missing\n'
[[ -f /usr/include/jpeglib.h ]] \
  && printf 'libjpeg_headers=present\n' \
  || printf 'libjpeg_headers=missing\n'

