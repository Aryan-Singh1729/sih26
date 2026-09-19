# Depth — reliable live depth acquisition

Implementation date: 2026-09-19

## Scope and status

The Depth code is implemented. It deliberately contains no browser server,
rays, 16×6 depth cone, obstacle clustering, EMEET capture, or motor controls.

The deterministic tests and build recipes are present. The UNO Q was not
reachable at `10.143.116.243:22` during this implementation session, and the
Windows host has no C++ compiler available. Consequently, the ARM64 compile,
live 15-minute run, measured-target checks, and physical disconnect test must
still be run on the UNO Q before the Depth exit gate can truthfully be marked
passed. This is an acceptance-test limitation, not a substitution with fake
sensor data.

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

The build-tree package configuration verified in Hardware is used directly:

```bash
cd /path/to/depth_dashboard
bash scripts/build.sh
```

Equivalent explicit commands:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -Drealsense2_DIR=/mnt/sdcard/librealsense/build
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
- Deterministic C++ tests on ARM64: PENDING — UNO Q unreachable during session.
- Live 15-minute SR300 run: PENDING — requires powered hardware.
- Measured-distance left/center/right check: PENDING — requires physical target.
- Invalid/no-return check: PENDING — requires powered hardware.
- Disconnect/restart check: PENDING — requires physical access.
- Depth exit gate: NOT YET PASSED.
