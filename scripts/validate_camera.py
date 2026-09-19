#!/usr/bin/env python3
import argparse
import hashlib
import json
import urllib.request


def health(base_url):
    with urllib.request.urlopen(base_url + "/health", timeout=5) as response:
        return json.load(response)


def read_frames(base_url, count):
    frames = []
    with urllib.request.urlopen(base_url + "/camera.mjpeg", timeout=10) as response:
        while len(frames) < count:
            line = response.readline()
            if not line:
                raise RuntimeError("camera stream ended")
            if not line.lower().startswith(b"content-length:"):
                continue
            length = int(line.split(b":", 1)[1])
            while response.readline() not in (b"\r\n", b"\n"):
                pass
            frame = response.read(length)
            end = frame.rfind(b"\xff\xd9")
            if len(frame) != length or not frame.startswith(b"\xff\xd8") or end < 2:
                raise RuntimeError("invalid JPEG frame")
            frames.append(frame[:end + 2])
    return frames


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base_url")
    parser.add_argument("--frames", type=int, default=10)
    args = parser.parse_args()
    base = args.base_url.rstrip("/")
    before = health(base)
    frames = read_frames(base, args.frames)
    after = health(base)

    if before["sr300_state"] != "live" or after["sr300_state"] != "live":
        raise RuntimeError(f"SR300 not live: {before} -> {after}")
    if before["emeet_state"] != "live" or after["emeet_state"] != "live":
        raise RuntimeError(f"EMEET not live: {before} -> {after}")
    if after["last_frame_number"] <= before["last_frame_number"]:
        raise RuntimeError("depth frames did not advance during video capture")
    if after["emeet_frame_number"] <= before["emeet_frame_number"]:
        raise RuntimeError("camera frames did not advance")
    if (after["emeet_width"], after["emeet_height"]) != (640, 360):
        raise RuntimeError(f"unexpected camera mode: {after}")
    hashes = {hashlib.sha256(frame).digest() for frame in frames}
    if len(hashes) < 2:
        raise RuntimeError("camera frames did not change")

    print("PASS", f"jpeg_frames={len(frames)}", f"unique={len(hashes)}",
          f"depth={before['last_frame_number']}->{after['last_frame_number']}",
          f"camera={before['emeet_frame_number']}->{after['emeet_frame_number']}",
          f"camera_fps={after['emeet_fps']}")


if __name__ == "__main__":
    main()
