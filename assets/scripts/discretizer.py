#!/usr/bin/env python3
"""Cut the rectified painting into a 36x28 LED grid.

Writes a PNG of the discrete cells and a FastLED header of CRGB values.
The header is the full painting; firmware can later omit cells that do
not sit over a physical LED.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2
import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[2]
ASSETS_DIR = REPO_ROOT / "assets"

DEFAULT_WIDTH = 36
DEFAULT_HEIGHT = 28
PREVIEW_CELL = 20


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=ASSETS_DIR / "input-exact.jpg",
        help="Perspective-corrected painting from reprojector",
    )
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument(
        "--png",
        type=Path,
        default=ASSETS_DIR / "discretized.png",
        help="Nearest-neighbor preview of the LED grid",
    )
    parser.add_argument(
        "--header",
        type=Path,
        default=REPO_ROOT / "include" / "painting_rgb.h",
        help="FastLED CRGB table for firmware",
    )
    parser.add_argument(
        "--cell",
        type=int,
        default=PREVIEW_CELL,
        help="Preview pixels per LED cell",
    )
    return parser.parse_args()


def sample_grid(image: np.ndarray, grid_w: int, grid_h: int) -> np.ndarray:
    """Average each logical cell to one RGB triple (uint8, RGB order)."""
    h, w = image.shape[:2]
    if image.ndim != 3 or image.shape[2] != 3:
        raise ValueError("Expected a 3-channel BGR image")
    rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB).astype(np.float64)
    grid = np.zeros((grid_h, grid_w, 3), dtype=np.uint8)
    for y in range(grid_h):
        y0 = int(y * h / grid_h)
        y1 = int((y + 1) * h / grid_h)
        for x in range(grid_w):
            x0 = int(x * w / grid_w)
            x1 = int((x + 1) * w / grid_w)
            cell = rgb[y0:y1, x0:x1]
            if cell.size == 0:
                raise RuntimeError(f"Empty cell at ({x}, {y})")
            grid[y, x] = np.clip(np.round(cell.mean(axis=(0, 1))), 0, 255)
    return grid


def write_preview(grid: np.ndarray, path: Path, cell: int) -> None:
    if cell < 1:
        raise ValueError("--cell must be >= 1")
    bgr = cv2.cvtColor(grid, cv2.COLOR_RGB2BGR)
    preview = cv2.resize(
        bgr,
        (grid.shape[1] * cell, grid.shape[0] * cell),
        interpolation=cv2.INTER_NEAREST,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(path), preview):
        raise RuntimeError(f"Failed to write {path}")


def format_crgb(rgb: np.ndarray) -> str:
    r, g, b = (int(v) for v in rgb)
    return f"CRGB({r:3d}, {g:3d}, {b:3d})"


def write_header(grid: np.ndarray, path: Path) -> None:
    height, width = grid.shape[:2]
    lines = [
        "#pragma once",
        "",
        "#include <FastLED.h>",
        "",
        f"#define PAINTING_WIDTH  {width}",
        f"#define PAINTING_HEIGHT {height}",
        "",
        "// Logical painting grid, row-major, origin at top-left.",
        "// Index as PAINTING_RGB[y][x] with x right and y down.",
        "// Larger than the 32x24 physical array (3x 8x32 panels);",
        "// firmware may crop or skip cells that have no LED behind them.",
        f"const CRGB PAINTING_RGB[PAINTING_HEIGHT][PAINTING_WIDTH] = {{",
    ]
    for y in range(height):
        cells = ", ".join(format_crgb(grid[y, x]) for x in range(width))
        comma = "," if y < height - 1 else ""
        lines.append(f"  {{ {cells} }}{comma}")
    lines.append("};")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines))


def main() -> None:
    args = parse_args()
    if args.width < 1 or args.height < 1:
        raise ValueError("Grid dimensions must be positive")

    image = cv2.imread(str(args.input), cv2.IMREAD_COLOR)
    if image is None:
        raise FileNotFoundError(f"Could not read image: {args.input}")

    grid = sample_grid(image, args.width, args.height)
    write_preview(grid, args.png, args.cell)
    write_header(grid, args.header)

    h, w = image.shape[:2]
    print(f"input:     {args.input} ({w}x{h})")
    print(f"grid:      {args.width} x {args.height} ({args.width * args.height} cells)")
    print(f"png:       {args.png} ({args.width * args.cell}x{args.height * args.cell})")
    print(f"header:    {args.header}")


if __name__ == "__main__":
    main()
