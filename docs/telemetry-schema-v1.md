# Raksh telemetry schema version 1

Deployment freezes the dashboard telemetry contract at `schema_version: 1`.
Fields may be added compatibly, but existing field names, meanings, units, and
risk labels must not change without introducing a new schema version.

Each `telemetry` Server-Sent Event contains:

- `schema_version`: integer `1`
- `frame_number`: SR300 depth frame number
- `monotonic_ms`: monotonic capture timestamp in milliseconds
- `sensor_status`: `live`, `stale`, `disconnected`, or `camera_missing`
- `stale`: boolean freshness flag
- `display_range_m`: ray-view range in metres
- `horizontal_fov_rad`: horizontal field of view in radians
- `cells`: 96 entries with `column`, `row`, `valid`, `distance_m`,
  `confidence`, and `risk`
- `rays`: 48 entries with `index`, `bearing_rad`, `range_m`, `valid`,
  `confidence`, and `risk`
- `clusters`: zero or more entries with `nearest_m`, `bearing_rad`, and
  `angular_width_rad`

The risk labels are `critical`, `close`, `intermediate`, `clear`, and
`invalid`. Null/invalid measurements always mean unknown; clients must never
interpret them as free space.
