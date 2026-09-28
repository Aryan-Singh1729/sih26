# Browser ray view and depth cone

Implementation and validation date: 2026-09-19

## Delivered

- A responsive single-page dashboard served directly by
  `raksh_dashboard_server`.
- A live 16×6 depth-cone panel whose 96 blocks use the risk classification from
  the SR300 perception service.
- A Canvas 2D top-down view with Raksh near the bottom center, a forward heading
  arrow, field-of-view edges, metric range rings, and 48 live depth rays.
- Metric ray geometry calculated from transmitted bearing and range. Colors do
  not determine ray length.
- Endpoint dots and metric labels for obstacle clusters.
- Consistent risk colors: coral critical, orange close, lavender intermediate,
  cyan/green clear, and dark gray unknown.
- Invalid rays are omitted and invalid cells remain dark gray; missing readings
  are never drawn as a maximum-range clear path.
- Visible `STARTING`, `LIVE`, `STALE`, and `DISCONNECTED` states. A local timer
  changes a formerly live view to stale when telemetry is older than 1.5 s.
- An EMEET panel with an explicit unavailable state; it does not show fake
  video.
- No keyboard listeners, motor commands, or control endpoints.

## Run

From the repository root on the UNO Q:

```bash
./build/raksh_dashboard_server \
  --serial 617205001375 \
  --bind 10.143.116.243 \
  --port 8080 \
  --web-root web
```

Replace the bind address if the UNO Q receives a different LAN address, then
open `http://<UNO-Q-address>:8080/` from the laptop.

## Rendering behavior

The browser keeps only the newest telemetry object. `requestAnimationFrame`
redraws the canvas smoothly, but sensor state changes only when a new SSE event
arrives. Each ray endpoint is calculated as:

```text
screen length = min(range metres / display range metres, 1) × radar radius
x = robot x + sin(bearing) × screen length
y = robot y - cos(bearing) × screen length
```

This keeps forward at the top, negative bearings to the left, and positive
bearings to the right. The layout collapses to the essential cone and ray view
on narrow screens without enabling any drive controls.

## Validation evidence

- JavaScript syntax validation passed with `node --check`.
- The final source compiled once on Debian 13 ARM64 against librealsense v2.50.0.
- All deterministic native tests passed (`3/3`).
- From the laptop, `/`, `/dashboard.css`, and `/dashboard.js` all returned HTTP
  200 from the UNO Q.
- A live three-client test received 84, 84, and 20 events in 10 seconds while
  frame numbers advanced from 1583 to 1840. Every inspected live event had
  exactly 96 cells and 48 rays.
- Source checks found no random-data generation, keyboard listeners, or motor
  route in the dashboard.

The camera area connects to the physical EMEET C950 without coupling webcam
failure to depth capture, and shows an explicit placeholder when unavailable.
