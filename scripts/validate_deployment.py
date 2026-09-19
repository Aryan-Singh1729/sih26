#!/usr/bin/env python3
import argparse
import json
import time
import urllib.request


def read_health(base):
    with urllib.request.urlopen(base + "/health", timeout=5) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("--seconds", type=int, default=7200)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    deadline = time.monotonic() + args.seconds
    first = None
    last = None
    maximum_depth_age = 0
    maximum_camera_age = 0
    samples = 0
    while time.monotonic() < deadline:
        current = read_health(base)
        if current["service_state"] != "live" or current["sr300_state"] != "live":
            raise RuntimeError(f"depth unhealthy: {current}")
        if current["emeet_state"] != "live":
            raise RuntimeError(f"camera unhealthy: {current}")
        if current["last_frame_age_ms"] > 1000 or current["emeet_frame_age_ms"] > 1500:
            raise RuntimeError(f"stale sensor: {current}")
        first = first or current
        last = current
        maximum_depth_age = max(maximum_depth_age, current["last_frame_age_ms"])
        maximum_camera_age = max(maximum_camera_age, current["emeet_frame_age_ms"])
        samples += 1
        time.sleep(1)
    if not first or last["last_frame_number"] <= first["last_frame_number"]:
        raise RuntimeError("depth frames did not advance")
    if last["emeet_frame_number"] <= first["emeet_frame_number"]:
        raise RuntimeError("camera frames did not advance")
    print("PASS", f"seconds={args.seconds}", f"samples={samples}",
          f"depth={first['last_frame_number']}->{last['last_frame_number']}",
          f"camera={first['emeet_frame_number']}->{last['emeet_frame_number']}",
          f"max_depth_age_ms={maximum_depth_age}",
          f"max_camera_age_ms={maximum_camera_age}")


if __name__ == "__main__":
    main()
