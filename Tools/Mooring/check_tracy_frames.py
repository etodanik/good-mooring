#!/usr/bin/env python3
"""Check paused QA frames exported by mooring-tracy-inspect --images-dir.

Requires Pillow. Record the complete --water-qa[-ultra] run with
--tracy-images=1. Unlike synchronous screenshots, these images retain
intermediate frames while two command buffers remain in flight.
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def paused_groups():
    # QA fixtures: scene, simulation warmup, number of diagnostic captures.
    # Each paused diagnostic spans three frames. Motion has a moving interval
    # before its paused checks; those moving frames must not be compared.
    cases = (
        ("calm", 150, 18), ("rough", 150, 20), ("storm", 150, 24),
        ("wake", 600, 20), ("dock", 150, 18), ("light", 150, 10),
        ("calm-low", 150, 5), ("glass", 150, 5), ("whitecaps", 150, 25),
        ("motion", 210, 9), ("catamaran", 600, 20),
    )
    offset = 0
    for scene, warmup, count in cases:
        for diagnostic in range(count):
            # Tracy frame references start at one.
            yield scene, offset + warmup + 1 + diagnostic * 3
        offset += warmup + count * 3 + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    failures = []
    maximum = 0.0
    groups = 0
    # Sky filtering can still converge by a few quantized thumbnail pixels
    # after simulation pauses. Allow 0.01 of one 8-bit level on average.
    tolerance = 0.01
    for scene, first in paused_groups():
        images = []
        for frame in range(first, first + 3):
            with Image.open(args.directory / f"{frame}.dds") as image:
                images.append(image.convert("RGB"))
        for index, image in enumerate(images[1:], 1):
            difference = ImageChops.difference(images[0], image)
            mean = sum(ImageStat.Stat(difference).mean) / 3
            maximum = max(maximum, mean)
            if mean > tolerance:
                failures.append({"scene": scene, "reference": first,
                                 "frame": first + index, "mean_error": mean})
        groups += 1
    report = {"groups": groups, "frames": groups * 3, "tolerance": tolerance,
              "maximum_mean_error": maximum, "mismatches": failures}
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{groups} paused groups; {len(failures)} mismatched frames; "
          f"maximum mean error {maximum:.6f}/255")
    if failures:
        print("FAIL: paused frames changed between screenshot readbacks.")
        return 1
    print("PASS: paused diagnostics remain stable on intermediate frames.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
