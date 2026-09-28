# Live Perception Dashboard for UGV

A real-time perception dashboard for an Arduino UNO Q based rover for demonstration purpose. It combines
an Intel RealSense SR300 depth stream with a front-facing EMEET C950 camera and
presents the result as a browser-based driving view:

- a live 16 × 6 depth grid with distance-based risk colours;
- a top-down ray view showing obstacle bearings and an open corridor;
- a low-latency MJPEG feed from the rover's front camera;
- explicit live, stale, disconnected, and unknown-data states;
- a full-screen depth grid for the rover's physical LCD.

The perception pipeline never treats invalid depth samples as measured free
space. Sensor capture, browser delivery, and rendering are kept independent so
a slow client cannot block either camera.

## Hardware

- Arduino UNO Q
- Intel RealSense SR300 (`617205001375` in the reference setup)
- EMEET C950 USB camera
- front LCD used for the depth grid
- SD card mounted at `/mnt/sdcard`

The UNO Q build expects librealsense 2.50.0 at
`/mnt/sdcard/librealsense`, with its compiled library in
`/mnt/sdcard/librealsense/build`.

## Build on the UNO Q

```bash
bash scripts/build.sh
```

The script configures CMake, compiles the services, and runs the deterministic
test suite. The resulting executables are written to `build/`:

```text
build/raksh_depth_service
build/raksh_perception_service
build/raksh_dashboard_server
```

## Start the dashboard

For normal operation, copy the example configuration and use the guarded
launcher:

```bash
cp config/raksh-dashboard.conf.example config/raksh-dashboard.conf
bash scripts/run_dashboard.sh config/raksh-dashboard.conf
```

Then open the address printed by the launcher, for example:

```text
http://10.143.116.243:8080/
```

The server can also be started directly:

```bash
./build/raksh_dashboard_server \
  --serial 617205001375 \
  --bind 0.0.0.0 \
  --port 8080 \
  --web-root web
```

Use an IPv4 bind address. `0.0.0.0` exposes the dashboard on every IPv4
interface; replace it with the UNO Q's LAN address when a restricted bind is
preferred.

## HTTP interface

- `GET /` serves the dashboard.
- `GET /health` reports service and sensor health.
- `GET /events` streams depth telemetry as Server-Sent Events.
- `GET /camera.mjpeg` streams the front camera.
- `GET /camera.jpg` returns the most recent camera frame.

Telemetry uses the stable `schema_version: 1` contract.

## Validation

Run the checks independently so failures are easy to isolate:

```bash
bash scripts/validate_depth.sh
python3 scripts/validate_perception.py ./build/raksh_perception_service
python3 scripts/validate_server.py http://127.0.0.1:8080 --seconds 30
python3 scripts/validate_camera.py http://127.0.0.1:8080 --frames 10
python3 scripts/validate_deployment.py http://127.0.0.1:8080 --seconds 7200
```

USB recovery checks are available separately:

```bash
bash scripts/test_depth_disconnect.sh
bash scripts/test_camera_disconnect.sh http://127.0.0.1:8080
```

## Project layout

```text
include/    C++ interfaces for depth, perception, and camera capture
src/        native capture, perception, and HTTP server implementations
web/        browser dashboard
config/     runtime configuration examples
scripts/    build, launch, diagnostics, and validation tools
tests/      deterministic native tests
```

