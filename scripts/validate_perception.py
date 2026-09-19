#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys


def expected_risk(distance):
    if distance <= 0.45:
        return "critical"
    if distance <= 0.90:
        return "close"
    if distance <= 1.80:
        return "intermediate"
    return "clear"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("service")
    parser.add_argument("--seconds", type=int, default=12)
    args = parser.parse_args()
    completed = subprocess.run(
        [args.service, "--duration-seconds", str(args.seconds), "--output-hz", "10"],
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode:
        sys.stderr.write(completed.stderr)
        return completed.returncode

    updates = [json.loads(line) for line in completed.stdout.splitlines() if line]
    if len(updates) < args.seconds * 6:
        raise RuntimeError(f"too few perception updates: {len(updates)}")
    if any(len(update["cells"]) != 96 for update in updates):
        raise RuntimeError("an update did not contain exactly 96 cells")
    if any(len(update["rays"]) != 48 for update in updates):
        raise RuntimeError("an update did not contain exactly 48 rays")
    if updates[-1]["frame"] <= updates[0]["frame"]:
        raise RuntimeError("frame numbers did not advance")

    valid_rays = []
    valid_cells = []
    for update in updates:
        for cell in update["cells"]:
            if cell["valid"]:
                valid_cells.append(cell)
            elif cell["distance_m"] is not None or cell["risk"] != "invalid":
                raise RuntimeError("invalid cell was represented as traversable depth")
        for ray in update["rays"]:
            if ray["valid"]:
                valid_rays.append(ray)
                if ray["risk"] != expected_risk(ray["range_m"]):
                    raise RuntimeError("ray risk does not match metric range")
            elif ray["range_m"] is not None or ray["risk"] != "invalid":
                raise RuntimeError("invalid ray was represented as traversable depth")

    print(
        "PASS",
        f"updates={len(updates)}",
        f"first_frame={updates[0]['frame']}",
        f"last_frame={updates[-1]['frame']}",
        "cells=96",
        "rays=48",
        f"valid_cells={len(valid_cells)}",
        f"valid_rays={len(valid_rays)}",
    )
    sys.stderr.write(completed.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
