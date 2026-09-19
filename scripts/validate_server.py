#!/usr/bin/env python3
import argparse
import json
import threading
import time
import urllib.request


def read_sse(url, stop, result, delay):
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            for raw in response:
                if stop.is_set():
                    break
                if not raw.startswith(b"data: "):
                    continue
                encoded = raw[6:]
                try:
                    payload = json.loads(encoded)
                except json.JSONDecodeError as error:
                    start = max(0, error.pos - 80)
                    end = min(len(encoded), error.pos + 80)
                    raise RuntimeError(
                        f"invalid telemetry JSON at {error.pos}: "
                        f"{encoded[start:end]!r}"
                    ) from error
                if payload["sensor_status"] == "live":
                    if len(payload["cells"]) != 96 or len(payload["rays"]) != 48:
                        raise RuntimeError("telemetry count mismatch")
                    result["events"] += 1
                    result["first_frame"] = result["first_frame"] or payload["frame_number"]
                    result["last_frame"] = payload["frame_number"]
                if delay:
                    time.sleep(delay)
    except Exception as error:
        if not stop.is_set():
            result["error"] = str(error)


def read_health(url):
    last_error = None
    for _ in range(3):
        try:
            with urllib.request.urlopen(url, timeout=5) as response:
                return json.load(response)
        except Exception as error:
            last_error = error
            time.sleep(0.2)
    raise RuntimeError(f"health unavailable after three attempts: {last_error}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("--seconds", type=int, default=900)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    stop = threading.Event()
    results = [
        {"events": 0, "first_frame": 0, "last_frame": 0, "error": ""}
        for _ in range(3)
    ]
    threads = [
        threading.Thread(target=read_sse,
                         args=(base + "/events", stop, results[index], delay),
                         daemon=True)
        for index, delay in enumerate((0.0, 0.0, 0.5))
    ]
    for thread in threads:
        thread.start()

    first_health_frame = 0
    last_health_frame = 0
    startup_deadline = time.monotonic() + 10
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        health = read_health(base + "/health")
        if health["service_state"] == "starting" and time.monotonic() < startup_deadline:
            time.sleep(0.2)
            continue
        if health["service_state"] != "live" or health["sr300_state"] != "live":
            raise RuntimeError(f"unhealthy state: {health}")
        if health["last_frame_age_ms"] > 1000:
            raise RuntimeError(f"stale frame: {health}")
        first_health_frame = first_health_frame or health["last_frame_number"]
        last_health_frame = health["last_frame_number"]
        time.sleep(1)

    stop.set()
    if last_health_frame <= first_health_frame:
        raise RuntimeError("health frame number did not advance")
    for index, result in enumerate(results):
        if result["error"]:
            raise RuntimeError(f"client {index} failed: {result['error']}")
        minimum = args.seconds * 5 if index < 2 else max(5, args.seconds)
        if result["events"] < minimum:
            raise RuntimeError(f"client {index} received only {result['events']} events")
        if result["last_frame"] <= result["first_frame"]:
            raise RuntimeError(f"client {index} frames did not advance")

    print("PASS", f"seconds={args.seconds}",
          f"health_frames={first_health_frame}->{last_health_frame}",
          "client_events=" + ",".join(str(item["events"]) for item in results))


if __name__ == "__main__":
    main()
