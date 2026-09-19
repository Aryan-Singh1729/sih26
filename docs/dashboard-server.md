# Server — local dashboard data server

Implementation and validation date: 2026-09-19

## Delivered

- `raksh_dashboard_server`, a small C++ HTTP server integrated with the live
  SR300 capture and Perception perception pipeline.
- `GET /health`, reporting service/SR300 state, last frame number and age,
  measured depth FPS, and the EMEET placeholder
  `unavailable_until_milestone_6`.
- `GET /events`, a one-way Server-Sent Events stream at approximately 10 Hz.
- A versioned schema containing live frame identity and freshness, display
  range/FOV, exactly 96 depth-cone cells, exactly 48 rays, and obstacle
  clusters.
- A bounded latest-state handoff. Capture keeps only the current serialized
  update; a client cannot create an application-level telemetry queue.
- Explicit `starting`, `live`, `stale`, `camera_missing`, and `disconnected`
  states. Invalid depth remains invalid/null rather than becoming free space.
- Configurable serial, IPv4 bind address, port, and optional run duration.
  The default bind address is loopback; use the UNO Q's exact LAN address for
  laptop access.
- Read-only behavior. Unknown paths, including `/motor`, return HTTP 404.

No random, simulated, or placeholder depth is used. The EMEET value is a
clearly labelled status placeholder only; camera transport belongs to Camera.

## Build and run on the UNO Q

```bash
bash scripts/build.sh
./build/raksh_dashboard_server \
  --serial 617205001375 --bind 10.143.116.243 --port 8080
```

Replace `10.143.116.243` with the UNO Q's current LAN address. Keep the service
on the robot's trusted local network.

Check it from the laptop:

```bash
curl http://10.143.116.243:8080/health
curl -N http://10.143.116.243:8080/events
```

Run the external acceptance client:

```bash
python scripts/validate_server.py http://10.143.116.243:8080 --seconds 900
```

The validator holds two normal SSE clients and one deliberately slow client,
polls health throughout the run, and rejects stale state, stopped frame
numbers, or incorrect live cell/ray counts.

## Telemetry contract

Each `telemetry` SSE event is one JSON object with `schema_version: 1`:

- `frame_number`, `monotonic_ms`, `sensor_status`, and `stale`
- `display_range_m` and `horizontal_fov_rad`
- `cells`: 16 columns × 6 rows, with validity, metric distance, confidence,
  and risk
- `rays`: 48 indexed bearings and metric ranges, with validity, confidence,
  and risk
- `clusters`: nearest metric distance, center bearing, and angular width

On capture failure, the next event explicitly sets `stale: true` and reports
the failure state. Consumers must treat absent/invalid measurements as unknown.

## Validation evidence

- ARM64 compilation completed against the installed librealsense v2.50.0.
- All three deterministic suites passed (`3/3`): sampler, acquisition state,
  and perception.
- An external laptop read live health and SSE data from the UNO Q. Frames
  advanced, depth stayed near 30 FPS, and every inspected live event contained
  exactly 96 cells and 48 rays.
- Multiple simultaneous clients, including a slow reader, consumed the live
  stream while health polling verified capture continuity.
- Health polling stayed live throughout the 15-minute external endurance
  window. At 11 minutes, capture was 29.97 FPS with 115 ms frame age; RSS was
  40,060 KiB versus a 39,772 KiB baseline (a bounded 288 KiB increase).
- A separate three-client confirmation passed with 67, 67, and 15 live events
  over 10 seconds. Its two normal clients and deliberately delayed client all
  received advancing frames with exact 96-cell/48-ray payloads.
- The acquisition path's physical disconnect/rebind behavior was already
  verified in Depth. Server maps a capture exception to an explicit
  disconnected/stale event rather than fabricated measurements or a crash.
- Source inspection found no random-data generator and no motor/control route.

Server intentionally serves data only. The Dashboard browser will render the
ray view and depth-cone panel; Camera will add the live EMEET feed.
