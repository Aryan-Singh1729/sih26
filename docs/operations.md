# Deployment — calibration, hardening, and operations

Implementation date: 2026-09-19

## Final software state

- Runtime calibration values are stored in
  `config/raksh-dashboard.conf.example` and passed to the service by
  `scripts/run_dashboard.sh`; threshold or mounting changes no longer require
  recompilation.
- The launcher checks the SD-card mount, pinned librealsense library, SR300 USB
  identity, EMEET USB identity and stable by-id node, web assets, executable,
  and configured TCP port before starting.
- Logs contain only lifecycle/error text—never raw images or depth frames—and
  rotate in `/tmp/raksh-dashboard` at 512 KiB with three retained files.
- Missing/disconnected sensors, stale frames, browser reconnects, and slow
  clients have explicit bounded behavior. EMEET recovery currently requires a
  service restart after physical reattachment.
- Telemetry schema 1 is frozen in `docs/telemetry-schema-v1.md`.
- No motor endpoint or keyboard drive listener exists.

## Build and configure

```bash
sudo mount /dev/sda1 /mnt/sdcard   # only if findmnt shows it is absent
bash scripts/build.sh
cp config/raksh-dashboard.conf.example config/raksh-dashboard.conf
```

Edit `config/raksh-dashboard.conf`. Set `BIND_ADDRESS` to the UNO Q's trusted
LAN address for laptop access. Measure the installed camera height and downward
pitch, then update `CAMERA_HEIGHT_M` and `CAMERA_PITCH_DEG`. Validate the three
risk boundaries with tape-measured targets before treating them as safety
thresholds.

## Start, check, and stop

Start in the foreground so failures remain visible:

```bash
bash scripts/run_dashboard.sh config/raksh-dashboard.conf
```

From the laptop:

```bash
curl http://<UNO-Q-IP>:8080/health
python scripts/validate_server.py http://<UNO-Q-IP>:8080 --seconds 30
python scripts/validate_camera.py http://<UNO-Q-IP>:8080 --frames 10
```

Open `http://<UNO-Q-IP>:8080/` in the browser. Stop cleanly with `Ctrl+C`.

## Final acceptance run

After physical calibration, keep the dashboard open and run:

```bash
python scripts/validate_deployment.py http://<UNO-Q-IP>:8080 --seconds 7200
```

During the two hours, record board temperature, process RSS, and free space in
a separate terminal:

```bash
watch -n 10 'ps -C raksh_dashboard_server -o pid,rss,etime,cmd; \
cat /sys/class/thermal/thermal_zone0/temp; df -h / /mnt/sdcard'
```

Walk obstacles through left, center, and right at tape-measured distances.
Check a narrow pole, broad wall, low object, angled surface, dark surface,
reflective surface, and no-return region. Confirm colors progress from clear to
intermediate, close, and critical as distance decreases.

## Troubleshooting

- `PREFLIGHT FAILED: /mnt/sdcard is not mounted`: mount `/dev/sda1` and retry.
- SR300 or EMEET missing: check `lsusb` for `8086:0aa5` and `328f:00ea`.
- EMEET disconnected after replug: stop and restart the launcher.
- Port already in use: stop the previous dashboard process or choose another
  `PORT` in the configuration.
- Dashboard stale: inspect `/health`, then `/tmp/raksh-dashboard/dashboard.log`.
- Incorrect obstacle position/risk: verify camera mounting measurements and
  calibration values; do not compensate by changing browser geometry.

Automatic startup is intentionally not installed yet. Enable it only after the
manual two-hour acceptance run passes on the assembled robot.

## Completed software acceptance evidence

- Final Debian 13 ARM64 build completed successfully against librealsense
  v2.50.0; all native regression suites passed (`3/3`).
- A two-minute integrated soak passed with 63 external health samples. SR300
  frames advanced from 272 to 3881 and EMEET frames from 171 to 2186.
- Maximum observed frame ages were 136 ms for depth and 58 ms for video.
- Process RSS after approximately three minutes was 41,860 KiB.
- Board temperature was 45.3 °C. Root storage had 968 MiB free and the SD card
  had 2.2 GiB free at the end of the test.

These checks complete software hardening. They do not substitute for the
documented tape-measure scene calibration or uninterrupted two-hour test on the
assembled robot, which require a person to position and verify real obstacles.
