# Hardware and software baseline

Date: 2026-09-19

This document records the live UNO Q inspection performed for the Raksh perception dashboard. Commands were executed over SSH against `ArduinoQ`. Raw camera frames were not recorded to the SD card.

## System

- Hostname: `ArduinoQ`
- Operating system: Debian GNU/Linux 13.2 (Trixie)
- Architecture: `arm64` / `aarch64`
- Kernel: `6.16.7-g0dd6551ae96b`
- RAM: 1.7 GiB total, approximately 1.3 GiB available during inspection
- Swap: 870 MiB
- Root filesystem: 9.8 GiB, 90% used, approximately 968 MiB free

The root filesystem has little headroom. Dashboard builds, generated artifacts, and any temporary camera data must not be placed there unnecessarily.

## External storage

- Block device: `/dev/sda1`
- Size: 3.7 GB raw / 3.6 GiB usable
- Filesystem: ext4
- Label: `realsense`
- UUID: `2d5d8e90-fcaf-4488-be0d-c08eeac752c7`
- Required mount point: `/mnt/sdcard`
- Usage after mounting: 1.3 GiB used, 2.2 GiB available (36% used)

The device was present but `/mnt/sdcard` was not initially mounted. It was mounted explicitly with:

```sh
sudo mount /dev/sda1 /mnt/sdcard
```

The mount was verified with `findmnt` before using the librealsense build. There is currently no `/etc/fstab` entry for this filesystem, so a reboot may require it to be mounted again. No filesystem formatting or repair was performed.

## Intel RealSense SR300

- USB identity: `8086:0aa5`
- Model: Intel RealSense SR300
- Serial number: `617205001375`
- Firmware: `3.21.0.0`
- USB connection: 5,000 Mbit/s SuperSpeed bus
- Sensors enumerated: coded-light depth sensor and RGB camera
- Required depth profile: `640x480`, 30 FPS, `Z16`
- Librealsense source: `/mnt/sdcard/librealsense/`
- Librealsense build: `/mnt/sdcard/librealsense/build/`
- Source tag: `v2.50.0`
- `rs-enumerate-devices` version: `2.50.0`

The RealSense build contains a generated CMake configuration at:

```text
/mnt/sdcard/librealsense/build/realsense2Config.cmake
```

It is not installed in the system pkg-config search path. Inspection found that
the generated configuration is not relocatable at the SD-card location: it
derives `/mnt/include`, which does not exist. Raksh builds must explicitly use
the source headers and the pinned build-tree shared library instead.

### Depth endurance result

The executable `/mnt/sdcard/librealsense/build/test_sr300` was found to be stale and did not match `test_sr300.cpp`; it still performed the old single-pixel check. It was not overwritten. The current robust 11x11 median source was compiled to `/tmp/raksh_depth_probe` for the test.

The 65-second test produced:

- 1,862 depth frames, approximately 28.6 FPS overall including startup/warm-up
- No RealSense errors
- One valid robust center-region measurement at 1.859 m
- Eleven valid pixels in that 11x11 sample
- 1,861 frames reporting no valid center-region depth
- No change in used SD-card blocks
- Clean camera shutdown after the timeout signal

The result proves that the live stream is stable and that zero/invalid depth is not converted into clear space. It also shows that the current center view was almost entirely outside the SR300's valid range during this unattended test. Physical target-based distance calibration remains a separate calibration task.

## EMEET webcam

- USB identity: `328f:00ea`
- Model: EMEET SmartCam C950
- Serial number: `A260409000103685`
- Driver: `uvcvideo`
- USB connection: 480 Mbit/s USB 2.0 hub
- Stable capture path: `/dev/v4l/by-id/usb-Sonix_Technology_Co.__Ltd._EMEET_SmartCam_C950_A260409000103685-video-index0`
- Current resolved node: `/dev/video4` (informational only; code must not depend on it)

The primary node supports native MJPEG at 30 FPS for:

- 1920x1080
- 1280x960
- 1280x720
- 1024x576
- 800x600
- 640x480
- 640x360

It also supports uncompressed YUYV at 640x480 and 640x360 at 30 FPS. The selected baseline mode is native MJPEG, 640x360, requested at 30 FPS.

A single 640x360 JPEG frame was captured to `/tmp` for visual inspection. It confirmed a real floor-level view from the robot's front-facing EMEET camera, not the SR300 RGB stream.

The camera accepted a 30 FPS request but delivered approximately 20 FPS with its current automatic-exposure settings. Runtime monitoring must use measured FPS rather than assuming the requested value.

### Webcam endurance result

The direct V4L2 test requested 1,200 frames using native MJPEG at 640x360. It completed normally in 61 seconds:

- 1,200 frames captured
- Final measured rate: 20.08 FPS
- No V4L2 failure
- No change in used SD-card blocks

An attempted FFmpeg null-output endurance run did not provide progress and was stopped by its 70-second guard timeout. Direct `v4l2-ctl` streaming was then used as the authoritative device-level test. FFmpeg remains suitable for one-frame capture, but its long-running behavior must be revisited before it is selected as a production transport.

## Simultaneous two-camera endurance result

The SR300 robust depth probe and the EMEET direct V4L2 stream ran together for 126 seconds.

- SR300 frames: 3,667 (approximately 29.3 FPS)
- SR300 errors: 0
- SR300 valid center-region frames: 4
- SR300 invalid center-region frames: 3,663
- EMEET frames: 2,400
- EMEET final measured rate: 20.08 FPS
- EMEET exit status: success
- Both USB identities remained present after the run
- Average CPU idle: 69.8%
- Minimum sampled CPU idle: 0% during a transient sample
- Memory used after the run: 430 MiB
- Memory available after the run: 1,309 MiB
- SD-card used blocks before and after: 1,259,884 (unchanged)

The kernel log contained repeated SR300 `Unknown video format` notices when the RealSense UVC interfaces were reopened. It did not contain a USB disconnect, bandwidth exhaustion, transfer failure, or camera reset during the simultaneous run.

## Build and runtime dependencies

Present:

- GCC/G++ 14.2.0
- CMake 3.31.6
- GNU Make 4.4.1
- pkg-config 1.8.1
- v4l2-ctl 1.30.1
- FFmpeg/ffprobe 7.1.5
- Git 2.47.3
- Linux V4L2 headers
- Librealsense 2.50.0 headers, shared library, and CMake build-tree configuration

Not present as system development packages:

- OpenCV pkg-config package
- Boost.Beast headers
- libjpeg development headers/pkg-config package
- libwebsockets or another dedicated WebSocket server library

The networking decision is to use the existing POSIX socket capability with HTTP plus Server-Sent Events unless a later implementation review demonstrates a need for a dedicated dependency. EMEET MJPEG frames can be passed through without JPEG re-encoding.

## Video-node separation

`v4l2-ctl --list-devices` reports:

- SR300: `/dev/video0` through `/dev/video3`, plus `/dev/media0` and `/dev/media1`
- EMEET SmartCam C950: `/dev/video4` and `/dev/video5`, plus `/dev/media2`
- Qualcomm Venus decoder: `/dev/video6` and `/dev/video7`

Only the EMEET by-id `video-index0` link is allowed in the dashboard configuration. This avoids collisions when `/dev/videoN` numbering changes.

## Test status

- System and storage inventory: PASS
- Librealsense version and SR300 enumeration: PASS
- Robust SR300 run for at least 60 seconds: PASS, with low scene validity documented
- Invalid zero-depth handling: PASS
- EMEET identity and format enumeration: PASS
- EMEET real-view preview: PASS
- EMEET independent 60-second stream: PASS (1,200 frames in 61 seconds)
- Simultaneous SR300 and EMEET two-minute stream: PASS
- No SD-card growth during stream tests: PASS

## Exit-gate result

**PASS.** Both sensors operate simultaneously, the EMEET camera is selected through a stable by-id path, invalid SR300 readings remain explicitly unknown, and the tests did not grow SD-card usage.
