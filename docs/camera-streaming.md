# Camera — live EMEET camera panel

Implementation and validation date: 2026-09-19

## Delivered

- An independent V4L2 capture module for the front-facing EMEET SmartCam C950.
- Selection through the stable by-id path ending in
  `A260409000103685-video-index0`; no `/dev/videoN` assumption is used.
- Native MJPEG capture at 640×360, requested at 20 FPS. The measured rate on
  the combined USB workload is approximately 16.7 FPS.
- Four memory-mapped V4L2 buffers with only the newest JPEG retained by the
  application.
- A browser-compatible `/camera.mjpeg` multipart stream with frame number and
  monotonic timestamp headers.
- The live feed in the dashboard's top-right panel with preserved aspect ratio
  and a visible starting/stale/disconnected fallback.
- EMEET state, frame number, frame age, FPS, width, and height in `/health`.
- Independent failure handling: an EMEET timeout or disconnect updates camera
  state but does not stop SR300 depth capture or telemetry.

No simulated camera image is used, and the UNO Q does not re-encode frames.

## Run

```bash
./build/raksh_dashboard_server \
  --serial 617205001375 \
  --bind 10.143.116.243 \
  --port 8080 \
  --web-root web
```

The default EMEET path and mode match the verified hardware. They can be
overridden when needed:

```bash
--camera-device /dev/v4l/by-id/<emeet-index0> \
--camera-width 640 --camera-height 360 --camera-fps 20
```

Open `http://10.143.116.243:8080/` from the laptop.

## Validation

```bash
python scripts/validate_camera.py http://10.143.116.243:8080 --frames 10
bash scripts/test_camera_disconnect.sh http://10.143.116.243:8080
```

Evidence from the physical UNO Q:

- Debian 13 ARM64 compilation passed against librealsense v2.50.0.
- All native regression suites passed (`3/3`).
- Health reported SR300 live at 29.98 FPS and EMEET live at 16.72 FPS,
  640×360, with both frame ages below 60 ms.
- Ten requested MJPEG frames were valid and all ten had distinct hashes.
- While those frames were received, depth advanced from frame 2614 to 2636
  and camera capture advanced from 1476 to 1489.
- The laptop downloaded about 2.5 MB from `/camera.mjpeg` in three seconds with
  HTTP 200.
- Three simultaneous telemetry clients received 96, 95, and 23 current events
  in 10 seconds while both cameras were active.
- During a guarded EMEET-only USB unbind, `/health` continued reporting the
  SR300 live at 29.95 FPS and explicitly reported EMEET `disconnected`.
- The guarded test rebound the device. Restarting the dashboard restored SR300
  and EMEET to live at 30.06 and 16.72 FPS respectively.

The current recovery contract after physical EMEET reconnection is an explicit
service restart. Automatic reopen can be considered during Deployment hardening.
