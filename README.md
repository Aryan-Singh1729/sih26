# Raksh depth acquisition service

This repository contains the Hardware hardware inventory, the Depth SR300
depth acquisition service, Perception perception products, and the Server local
HTTP/SSE data server. Browser rendering, webcam streaming, and robot controls
are intentionally not included yet.

## Build on the Arduino UNO Q

The SD card must be mounted at `/mnt/sdcard`, with the verified librealsense
v2.50.0 build at `/mnt/sdcard/librealsense/build`.

```bash
bash scripts/build.sh
```

The build script compiles the deterministic tests, runs them, and creates:

```text
build/raksh_depth_service
```

## Run

Run until `Ctrl+C`:

```bash
./build/raksh_depth_service --serial 617205001375
```

Run the repeatable 15-minute acceptance check:

```bash
bash scripts/validate_depth.sh
```

Run the guarded USB disconnect/rebind and restart check:

```bash
bash scripts/test_depth_disconnect.sh
```

All normal diagnostics are concise text records. An invalid depth region is
printed as `distance_m=unknown`; it is never converted to zero metres or clear
space. See [the Depth notes](docs/depth-service.md) for the diagnostic
contract and physical test checklist.

## Perception live perception

```bash
./build/raksh_perception_service --output-hz 10
python3 scripts/validate_perception.py ./build/raksh_perception_service
```

The service writes one JSON object per current live update with exactly 96
depth-cone cells, 48 rays, and optional obstacle clusters. Details and current
calibration limits are in [the Perception notes](docs/perception.md).

## Server local data server

Bind to the UNO Q's actual LAN address so the dashboard laptop can connect:

```bash
./build/raksh_dashboard_server \
  --serial 617205001375 --bind 10.143.116.243 --port 8080 --web-root web
```

The read-only endpoints are `GET /health` and `GET /events`. The latter is a
Server-Sent Events stream carrying schema version 1 at approximately 10 Hz.
There are no control or motor endpoints. See
[the Server notes](docs/dashboard-server.md) for the message contract and test
evidence.

Open `http://10.143.116.243:8080/` on the dashboard laptop for the top-down ray
display, live 16×6 depth cone, and Camera EMEET camera feed. The camera uses
the stable by-id device path and native MJPEG, so frames are not re-encoded on
the UNO Q.

Validate both physical cameras together:

```bash
python scripts/validate_camera.py http://10.143.116.243:8080 --frames 10
```

## Final hardened launcher

Copy and calibrate the runtime configuration, then use the preflight launcher:

```bash
cp config/raksh-dashboard.conf.example config/raksh-dashboard.conf
bash scripts/run_dashboard.sh config/raksh-dashboard.conf
```

The complete calibration, acceptance, and troubleshooting procedure is in
[the Deployment operations guide](docs/operations.md).
