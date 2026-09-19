# Depth — reliable live depth acquisition

Implementation date: 2026-09-19

## Scope and status

The Depth code is implemented. It deliberately contains no browser server,
rays, 16×6 depth cone, obstacle clustering, EMEET capture, or motor controls.

The service has been compiled and tested natively on the UNO Q. The deterministic
tests, 15-minute live run, invalid-depth behavior, missing-camera handling, and
disconnect/restart behavior passed on 2026-09-19. Tape-measured target checks at
left, center, and right remain a manual physical calibration check because the
camera's current view returned no valid depth in those three regions.

## What was implemented

- `raksh_depth_core`, a dependency-free C++17 library containing robust depth
  sampling and latest-frame acquisition state.
- `raksh_depth_service`, linked to the pinned librealsense v2.50.0 CMake target.
- A depth-only request for `640×480`, 30 FPS, `Z16`, locked to SR300 serial
  `617205001375` by default.
- Startup reporting of the active depth profile, intrinsics (`fx`, `fy`, `ppx`,
  `ppy`), and metric depth scale.
- Per-frame monotonic time, RealSense frame number, dimensions, depth scale,
  current interval FPS, and left/center/right robust-region diagnostics.
- An overwrite-only diagnostic store: a new frame replaces the previous frame;
  no historical frame queue exists.
- Explicit `starting`, `live`, `stale`, `camera_missing`, `start_failed`,
  `frame_timeout`, `runtime_error`, and `stopped` states.
- Signal- and duration-based orderly shutdown that calls `pipeline.stop()`.

## Robust sampling contract

The service reads an 11×11 region by default. The reusable sampler:

1. Rejects zero, non-finite, below-range, and above-range measurements.
2. Calculates the median and median absolute deviation of remaining samples.
3. Keeps values within the larger of 30 mm or three MADs from the median.
4. Defines confidence as post-filter inliers divided by all requested pixels.
5. Emits a distance only when the confidence gate is met. Otherwise the result
   is explicitly unknown with `valid=false` and `distance_m=0` internally.

The diagnostic formatter prints `unknown`, not `0.000`, for an invalid result.
This prevents downstream code from treating no-return pixels as free space.

## Build and deterministic tests on the UNO Q

The Hardware source and build trees are used directly. The generated
`realsense2Config.cmake` contains an install-prefix path and is not relocatable
at `/mnt/sdcard`; the Raksh build therefore imports the verified v2.50.0 shared
library and headers without modifying librealsense:

```bash
cd /path/to/depth_dashboard
bash scripts/build.sh
```

Equivalent explicit commands:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DREALSENSE_SOURCE_DIR=/mnt/sdcard/librealsense \
  -DREALSENSE_BUILD_DIR=/mnt/sdcard/librealsense/build
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Expected tests are `depth_sampler_test` and `depth_state_test`. They cover
invalid/zero/non-finite values, valid-ratio gates, even medians, isolated
outlier removal, post-filter confidence failure, overwrite-only latest-frame
storage, monotonic age, stale detection, and stable error-state names.

## Live diagnostics

```bash
./build/raksh_depth_service \
  --serial 617205001375 \
  --report-ms 1000
```

A healthy record has this shape:

```text
state=live frame=... monotonic_ms=... age_ms=... width=640 height=480 depth_scale_m=... fps=... left_valid=... left_distance_m=... left_confidence=... center_valid=... center_distance_m=... center_confidence=... right_valid=... right_distance_m=... right_confidence=...
```

The numbers above are field names only; the service never inserts simulated
values. Every distance comes from `rs2::depth_frame::get_distance()` in the
current live frame.

Exit codes are:

- `0`: requested duration or signal completed cleanly.
- `2`: invalid command line.
- `3`: configured camera serial is missing.
- `4`: frame timeout; restart required.
- `5`: librealsense start/runtime error.
- `6`: other runtime error.

## Repeatable 15-minute acceptance test

```bash
RAKSH_VALIDATION_SECONDS=900 bash scripts/validate_depth.sh
```

The harness stores short-lived diagnostics in `/tmp`, not on the 4 GB SD card.
It fails if acquisition reports an unhealthy state, lacks an orderly stop,
averages outside 27–33 FPS, or has more than 12 MiB post-warmup RSS variation.
It prints the exact log paths for review.

## Physical checks required for the exit gate

Run these after the build/tests pass:

1. Put a flat target at manually measured distances, including approximately
   0.5 m, 1.0 m, and 2.0 m. Record tape-measure and reported values.
2. Repeat at the left, center, and right sample regions.
3. Cover the sensor and aim it outside reliable range. Confirm affected regions
   say `valid=0` and `distance_m=unknown`.
4. During a live run, unplug the SR300. Confirm a controlled `frame_timeout` or
   `runtime_error`, nonzero exit, no crash trace, and `restart_required=1`.
5. Reconnect and start the service again. Confirm it returns to `state=live`.
6. Review the 15-minute `/tmp` diagnostics and RSS samples. Preserve the command,
   result, date, FPS, and memory range in this document.

## Acceptance record

- Source-level Depth implementation: COMPLETE (2026-09-19).
- Native ARM64 configure/build: PASS. GCC 14.2.0 linked the service against
  `/mnt/sdcard/librealsense/build/librealsense2.so.2.50.0`.
- Deterministic C++ tests on ARM64: PASS, 2/2 tests, zero failures.
- Live 15-minute SR300 run: PASS, 900 seconds and 26,955 frames.
- Average reported depth rate: PASS, 29.96 FPS.
- Post-warmup RSS stability: PASS, 39,436–39,528 KiB (92 KiB span across
  91 samples).
- Active profile: PASS, `640×480 @ 30 FPS Z16`; depth scale `0.00012499 m`.
- Invalid/no-return check: PASS. Left, center, and right regions consistently
  reported `valid=0`, `distance_m=unknown`, confidence `0.000`; none were
  represented as clear space.
- Missing-camera check: PASS. A nonexistent serial produced
  `state=camera_missing` and exit code `3`.
- Disconnect/restart check: PASS. A guarded USB unbind of exact device
  `8086:0aa5` (`2-1`) produced `state=frame_timeout` and exit code `4`; rebind
  followed by a new run returned to `state=live` and stopped cleanly.
- Orderly shutdown: PASS, `state=stopped`, `frames=26955`,
  `detail="duration completed"`.
- Measured-distance left/center/right check: PENDING — requires placing a
  tape-measured physical target in each region; the unattended scene supplied
  no valid returns at those sample locations.
- Depth exit gate: PASS for its stated requirements (15-minute stability,
  bounded latest-state design, stable memory, and safe invalid-depth handling).

Test artifacts on the UNO Q:

```text
/tmp/raksh-depth-20260919T102925Z.log
/tmp/raksh-depth-20260919T102925Z-rss-kib.log
/tmp/raksh-depth-disconnect-20260919T104746Z.log
/tmp/raksh-depth-disconnect-20260919T104746Z-restart.log
```
