# Raksh depth acquisition service

This repository currently contains Hardware hardware inventory and the Depth
SR300 depth acquisition service. It does not yet contain dashboard rendering,
rays, a depth cone, webcam streaming, or robot controls.

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
