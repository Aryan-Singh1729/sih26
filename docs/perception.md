# Live perception products

Implementation and validation date: 2026-09-19

## Delivered

- A live SR300 perception executable fixed at `640×480 @ 30 FPS Z16`.
- Exactly 96 depth-cone cells (`16×6`) using a 20th-percentile near distance.
- Exactly 48 horizontal rays deprojected with the active SR300 intrinsics and
  `rs2_deproject_pixel_to_point`.
- Metric bearing and ground-plane range, configurable valid range, risk bands,
  floor-plane rejection, temporal smoothing/hysteresis, and adjacent-ray
  obstacle clusters.
- JSON Lines output suitable for the local data server. Invalid depth
  is emitted as `valid=false`, `distance_m=null`/`range_m=null`, and
  `risk="invalid"`; no random or simulated values exist in the live path.

## Commands

```bash
./build/raksh_perception_service --output-hz 10
python3 scripts/validate_perception.py ./build/raksh_perception_service
```

Mount calibration can be supplied without recompiling:

```bash
./build/raksh_perception_service \
  --camera-height-m 0.40 --camera-pitch-deg 0
```

`--show-floor` preserves unfiltered returns for diagnostics.

## Test evidence

The ARM64 build passed all three suites: depth sampler, acquisition state, and
perception (`3/3`, zero failures). Tests cover exact cell/ray counts, narrow
obstacles, invalid depth, bearing orientation, risk monotonicity, floor
filtering and unfiltered diagnostics, immediate approaching-hazard updates,
hysteresis, and cluster separation.

A 12-second live run produced 100 JSON updates while SR300 frames advanced from
0 to 346. Every update contained 96 cells and 48 rays. The unattended scene
provided 900 valid cell measurements and 813 valid ray measurements. Current
returns were on the right (ray indices 39–47), with ray ranges 0.741–0.803 m,
cell distances 0.647–0.725 m, and one cluster per update. All risk assignments
matched their metric thresholds. Invalid values remained explicit unknowns.

The live structural exit gate is passed. Moving a tape-measured target through
left, center, and right remains a physical calibration follow-up; software tests
independently verify the bearing signs and distance/risk monotonicity.
