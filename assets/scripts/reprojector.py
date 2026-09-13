#!/usr/bin/env python3
"""Warp the photographed painting onto a perfect rectangle.

Reads four image-space corners from assets/corners.txt (one `x,y` per line)
and writes the perspective-corrected canvas to assets/input-exact.jpg.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np

ASSETS_DIR = Path(__file__).resolve().parent.parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=ASSETS_DIR / "input.jpg",
        help="Source photograph of the painting",
    )
    parser.add_argument(
        "--corners",
        type=Path,
        default=ASSETS_DIR / "corners.txt",
        help="Four x,y corners of the painting, one per line",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=ASSETS_DIR / "input-exact.jpg",
        help="Perspective-corrected rectangle",
    )
    return parser.parse_args()


def load_corners(path: Path) -> np.ndarray:
    points: list[list[float]] = []
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.replace(" ", ",").split(",") if p.strip()]
        if len(parts) != 2:
            raise ValueError(f"Expected `x,y` in {path}, got: {raw!r}")
        points.append([float(parts[0]), float(parts[1])])
    if len(points) != 4:
        raise ValueError(f"Expected 4 corners in {path}, found {len(points)}")
    return np.array(points, dtype=np.float32)


def order_corners(pts: np.ndarray) -> np.ndarray:
    """Return corners as top-left, top-right, bottom-right, bottom-left."""
    s = pts.sum(axis=1)
    d = np.diff(pts, axis=1).reshape(-1)
    tl = pts[np.argmin(s)]
    br = pts[np.argmax(s)]
    tr = pts[np.argmin(d)]
    bl = pts[np.argmax(d)]
    ordered = np.stack([tl, tr, br, bl]).astype(np.float32)
    if len({tuple(p) for p in ordered}) != 4:
        raise ValueError("Corner ordering produced duplicates; check corners.txt")
    return ordered


def destination_size(ordered: np.ndarray) -> tuple[int, int]:
    tl, tr, br, bl = ordered
    width = max(np.linalg.norm(tr - tl), np.linalg.norm(br - bl))
    height = max(np.linalg.norm(bl - tl), np.linalg.norm(br - tr))
    return max(1, int(round(width))), max(1, int(round(height)))


def main() -> None:
    args = parse_args()
    image = cv2.imread(str(args.input), cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(f"Could not read image: {args.input}")

    h, w = image.shape[:2]
    ordered = order_corners(load_corners(args.corners))
    for x, y in ordered:
        if not (0 <= x < w and 0 <= y < h):
            raise ValueError(f"Corner ({x}, {y}) is outside {w}x{h} image")

    dest_w, dest_h = destination_size(ordered)
    destination = np.array(
        [
            [0, 0],
            [dest_w - 1, 0],
            [dest_w - 1, dest_h - 1],
            [0, dest_h - 1],
        ],
        dtype=np.float32,
    )
    matrix = cv2.getPerspectiveTransform(ordered, destination)
    warped = cv2.warpPerspective(
        image,
        matrix,
        (dest_w, dest_h),
        flags=cv2.INTER_CUBIC,
        borderMode=cv2.BORDER_REPLICATE,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(
        str(args.output),
        warped,
        [int(cv2.IMWRITE_JPEG_QUALITY), 100],
    )
    if not ok:
        raise RuntimeError(f"Failed to write {args.output}")

    tl, tr, br, bl = ordered
    print(f"input:     {args.input} ({w}x{h})")
    print(f"corners:   {args.corners}")
    print(f"  TL {tuple(tl.tolist())}")
    print(f"  TR {tuple(tr.tolist())}")
    print(f"  BR {tuple(br.tolist())}")
    print(f"  BL {tuple(bl.tolist())}")
    print(f"output:    {args.output} ({dest_w}x{dest_h})")


if __name__ == "__main__":
    main()
