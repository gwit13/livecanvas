#!/usr/bin/env python3
"""Render assets/categorized.csv as a color-coded PNG.

Each CSV cell is a zone name. Pixel (col, row) matches the 36x28
painting grid from the discretizer.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
ASSETS_DIR = REPO_ROOT / "assets"

LABELS = (
    "foreground",
    "background",
    "far-clouds",
    "near-clouds",
    "sky",
)

# Distinct preview colors (RGB) for later effect targeting.
COLORS_RGB = {
    "sky": (168, 186, 204),
    "far-clouds": (232, 150, 148),
    "near-clouds": (236, 196, 120),
    "background": (196, 78, 58),
    "foreground": (40, 46, 42),
}

GRID_WIDTH = 36
GRID_HEIGHT = 28
PREVIEW_CELL = 20


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--csv",
        type=Path,
        default=ASSETS_DIR / "categorized.csv",
    )
    parser.add_argument(
        "--png",
        type=Path,
        default=ASSETS_DIR / "categorized.png",
    )
    parser.add_argument("--cell", type=int, default=PREVIEW_CELL)
    return parser.parse_args()


def load_grid(path: Path) -> list[list[str]]:
    rows: list[list[str]] = []
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cells = [cell.strip() for cell in line.split(",") if cell.strip()]
        if len(cells) != GRID_WIDTH:
            raise ValueError(
                f"{path}:{line_no}: expected {GRID_WIDTH} cells, got {len(cells)}"
            )
        unknown = sorted({c for c in cells if c not in COLORS_RGB})
        if unknown:
            raise ValueError(
                f"{path}:{line_no}: unknown label(s) {unknown}; "
                f"expected one of {', '.join(LABELS)}"
            )
        rows.append(cells)
    if len(rows) != GRID_HEIGHT:
        raise ValueError(
            f"{path}: expected {GRID_HEIGHT} rows, got {len(rows)}"
        )
    return rows


def render(grid: list[list[str]], cell: int) -> np.ndarray:
    if cell < 1:
        raise ValueError("--cell must be >= 1")
    image = np.zeros((GRID_HEIGHT, GRID_WIDTH, 3), dtype=np.uint8)
    for y, row in enumerate(grid):
        for x, label in enumerate(row):
            image[y, x] = COLORS_RGB[label]
    bgr = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)
    return cv2.resize(
        bgr,
        (GRID_WIDTH * cell, GRID_HEIGHT * cell),
        interpolation=cv2.INTER_NEAREST,
    )


def main() -> None:
    args = parse_args()
    grid = load_grid(args.csv)
    image = render(grid, args.cell)
    args.png.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(args.png), image):
        raise RuntimeError(f"Failed to write {args.png}")

    counts: dict[str, int] = {name: 0 for name in LABELS}
    for row in grid:
        for label in row:
            counts[label] += 1

    print(f"csv:    {args.csv} ({GRID_WIDTH}x{GRID_HEIGHT})")
    print(f"png:    {args.png} ({GRID_WIDTH * args.cell}x{GRID_HEIGHT * args.cell})")
    print("labels:")
    for name in LABELS:
        r, g, b = COLORS_RGB[name]
        print(f"  {name:12} {counts[name]:4d}  RGB({r},{g},{b})")


if __name__ == "__main__":
    main()
