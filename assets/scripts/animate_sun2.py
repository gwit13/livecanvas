#!/usr/bin/env python3
"""Load painting_rgb.h and categorized-human.csv, animate a sun with warmth.

Same hemispheric sun path as animate_sun.py. Each render tick warms and
dims the painting by zone when the sun is low (sunrise / sunset):

  height 0 at the horizon (row 20), 1 at the apex (depends on alignment)
  low-sun = 1 - height, so noon is the original painting
  near-clouds / foreground : up to +30% warmth and a bit darker
  sky                      : 0.7 as much
  far-clouds               : 0.5 as much
  background               : 0.2 as much

Warmth is a per-pixel amber skew (raise red, cut blue) plus a modest
dim so dawn/dusk read darker, not brighter. The sun blob is composited
after the warmth pass.
"""

from __future__ import annotations

import argparse
import re
import time
import tkinter as tk
from pathlib import Path

import numpy as np
from PIL import Image, ImageTk

from panel_align import Alignment, add_origin_args, bind_origin_keys, crop_to_panel

REPO_ROOT = Path(__file__).resolve().parents[2]
ASSETS_DIR = REPO_ROOT / "assets"

DEFAULT_HEADER = REPO_ROOT / "include" / "painting_rgb.h"
DEFAULT_CSV = ASSETS_DIR / "categorized-human.csv"
PREVIEW_CELL = 20

LABELS = (
    "foreground",
    "background",
    "far-clouds",
    "near-clouds",
    "sky",
)

# Relative warmth vs the 30% near-cloud / foreground cap.
WARMTH_SCALE = {
    "near-clouds": 1.0,
    "foreground": 1.0,
    "sky": 0.7,
    "far-clouds": 0.5,
    "background": 0.2,
}
MAX_WARMTH = 0.30
MAX_DARKEN = 0.25

# Channel mix for "warmer": R up, G a bit, B down. Applied as
# rgb * (1 + amount * WARM_GAIN) * (1 - low * MAX_DARKEN * scale).
WARM_GAIN = np.array([1.0, 0.40, -1.0])

align = Alignment()

SUN_RADIUS = 2.0
SUN_RGB = np.array([255.0, 214.0, 64.0])
DURATION_S = 10.0
FPS = 3

CRGB_RE = re.compile(r"CRGB\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")
WIDTH_RE = re.compile(r"#define\s+PAINTING_WIDTH\s+(\d+)")
HEIGHT_RE = re.compile(r"#define\s+PAINTING_HEIGHT\s+(\d+)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    add_origin_args(parser)
    parser.add_argument("--duration", type=float, default=DURATION_S)
    parser.add_argument("--fps", type=float, default=FPS)
    parser.add_argument("--cell", type=int, default=PREVIEW_CELL)
    parser.add_argument(
        "--loop",
        action="store_true",
        help="Replay the 10s path until the window is closed",
    )
    parser.add_argument(
        "--gif",
        type=Path,
        default=None,
        help="Also write an animated GIF (preview-scale)",
    )
    parser.add_argument(
        "--no-window",
        action="store_true",
        help="Skip the preview window (use with --gif)",
    )
    return parser.parse_args()


def load_painting(path: Path) -> np.ndarray:
    """Parse PAINTING_RGB into an (H, W, 3) uint8 array, RGB order."""
    text = path.read_text()
    width_m = WIDTH_RE.search(text)
    height_m = HEIGHT_RE.search(text)
    if width_m is None or height_m is None:
        raise ValueError(f"{path}: missing PAINTING_WIDTH / PAINTING_HEIGHT")
    width = int(width_m.group(1))
    height = int(height_m.group(1))

    rgb = np.array(
        [(int(r), int(g), int(b)) for r, g, b in CRGB_RE.findall(text)],
        dtype=np.uint8,
    )
    expected = width * height
    if rgb.shape[0] != expected:
        raise ValueError(
            f"{path}: expected {expected} CRGB entries "
            f"({height}x{width}), got {rgb.shape[0]}"
        )
    return rgb.reshape(height, width, 3)


def load_categories(path: Path, height: int, width: int) -> np.ndarray:
    """Zone labels from a CSV; pixel (row, col) matches the painting grid."""
    rows: list[list[str]] = []
    for line_no, raw in enumerate(path.read_text().splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cells = [cell.strip() for cell in line.split(",") if cell.strip()]
        if len(cells) != width:
            raise ValueError(
                f"{path}:{line_no}: expected {width} cells, got {len(cells)}"
            )
        unknown = sorted({c for c in cells if c not in WARMTH_SCALE})
        if unknown:
            raise ValueError(
                f"{path}:{line_no}: unknown label(s) {unknown}; "
                f"expected one of {', '.join(LABELS)}"
            )
        rows.append(cells)
    if len(rows) != height:
        raise ValueError(f"{path}: expected {height} rows, got {len(rows)}")
    return np.array(rows)


def warmth_scale_map(labels: np.ndarray) -> np.ndarray:
    """(H, W) float: 1.0 for full 30% zones, fractions for the others."""
    scale = np.zeros(labels.shape, dtype=np.float64)
    for name, value in WARMTH_SCALE.items():
        scale[labels == name] = value
    return scale


def sun_position(t: float) -> tuple[float, float]:
    """Floating-point (row, col) on the hemisphere.

    t is 0 at the left horizon and 1 at the right. Constant angular speed
    so the sun spends equal time climbing and falling.
    """
    return align.sky_position(t)


def sun_height(row: float) -> float:
    """0 on the horizon, 1 at the hemispheric apex."""
    return align.sun_height(row)


def apply_warmth(
    rgb: np.ndarray,
    scale: np.ndarray,
    height: float,
) -> np.ndarray:
    """Amber-skew and dim `rgb` by zone. Strongest at the horizon.

    `height` is 0 on the horizon and 1 at noon; warmth uses (1 - height).
    Returns float64 in [0, 255].
    """
    low = 1.0 - float(np.clip(height, 0.0, 1.0))
    weight = low * scale[..., np.newaxis]
    amount = MAX_WARMTH * weight
    dim = 1.0 - MAX_DARKEN * weight
    warmed = rgb.astype(np.float64) * (1.0 + amount * WARM_GAIN) * dim
    return np.clip(warmed, 0.0, 255.0)


def splat_sun(
    frame: np.ndarray,
    row: float,
    col: float,
    radius: float = SUN_RADIUS,
) -> np.ndarray:
    """Composite a disk at a sub-pixel (row, col). `frame` is float RGB."""
    out = np.array(frame, dtype=np.float64, copy=True)
    height, width = out.shape[:2]
    pad = radius + 1.0
    r0 = max(0, int(np.floor(row - pad)))
    r1 = min(height, int(np.ceil(row + pad)) + 1)
    c0 = max(0, int(np.floor(col - pad)))
    c1 = min(width, int(np.ceil(col + pad)) + 1)
    if r0 >= r1 or c0 >= c1:
        return np.clip(np.round(out), 0, 255).astype(np.uint8)

    yy, xx = np.mgrid[r0:r1, c0:c1]
    dist = np.hypot(xx - col, yy - row)
    aa = 0.25
    alpha = np.clip((radius - dist) / aa, 0.0, 1.0)[..., np.newaxis]
    patch = out[r0:r1, c0:c1]
    out[r0:r1, c0:c1] = patch * (1.0 - alpha) + SUN_RGB * alpha
    return np.clip(np.round(out), 0, 255).astype(np.uint8)


def upscale(grid: np.ndarray, cell: int) -> np.ndarray:
    """Nearest-neighbor scale of an RGB grid. Returns RGB uint8."""
    if cell < 1:
        raise ValueError("--cell must be >= 1")
    return np.repeat(np.repeat(grid, cell, axis=0), cell, axis=1)


def render_tick(
    painting: np.ndarray,
    scale: np.ndarray,
    t: float,
) -> np.ndarray:
    row, col = sun_position(t)
    warmed = apply_warmth(painting, scale, sun_height(row))
    return splat_sun(warmed, row, col)


def write_gif(frames_rgb: list[np.ndarray], path: Path, fps: float) -> None:
    if not frames_rgb:
        raise ValueError("No frames to write")
    images = [Image.fromarray(frame) for frame in frames_rgb]
    duration_ms = max(1, int(round(1000.0 / fps)))
    path.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(
        path,
        save_all=True,
        append_images=images[1:],
        duration=duration_ms,
        loop=0,
    )


def run_window(
    painting: np.ndarray,
    scale: np.ndarray,
    duration: float,
    fps: float,
    cell: int,
    loop: bool,
    collect: bool,
) -> list[np.ndarray]:
    """Show the animation in a Tk window. opencv-python-headless has no GUI."""
    recorded: list[np.ndarray] = []
    delay_ms = max(1, int(round(1000.0 / fps)))
    def set_title() -> None:
        root.title(f"sun  {align.title_suffix()}")

    root = tk.Tk()
    set_title()
    root.resizable(False, False)
    label = tk.Label(root, borderwidth=0, highlightthickness=0)
    label.pack()

    state = {
        "t0": time.perf_counter(),
        "collect": collect,
        "photo": None,
        "closed": False,
    }

    def close() -> None:
        if state["closed"]:
            return
        state["closed"] = True
        root.destroy()

    def tick() -> None:
        if state["closed"]:
            return
        elapsed = time.perf_counter() - state["t0"]
        t = min(elapsed / duration, 1.0) if duration > 0 else 1.0
        preview = upscale(crop_to_panel(render_tick(painting, scale, t), align), cell)
        if state["collect"]:
            recorded.append(preview)
        photo = ImageTk.PhotoImage(Image.fromarray(preview))
        state["photo"] = photo
        label.configure(image=photo)
        if t < 1.0:
            root.after(delay_ms, tick)
            return
        if loop:
            state["collect"] = False
            state["t0"] = time.perf_counter()
            root.after(delay_ms, tick)
            return
        root.after(delay_ms, close)

    bind_origin_keys(root, align, set_title)
    root.bind("<Escape>", lambda _e: close())
    root.bind("q", lambda _e: close())
    root.bind("Q", lambda _e: close())
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(0, tick)
    root.mainloop()
    return recorded


def main() -> None:
    args = parse_args()
    if args.duration <= 0:
        raise ValueError("--duration must be > 0")
    if args.fps <= 0:
        raise ValueError("--fps must be > 0")
    if args.no_window and args.gif is None:
        raise ValueError("--no-window requires --gif")

    global align
    align = Alignment(args.origin_x, args.origin_y)
    painting = load_painting(args.header)
    height, width = painting.shape[:2]
    labels = load_categories(args.csv, height, width)
    scale = warmth_scale_map(labels)
    print(f"grid:      {width} x {height}")
    print(f"zones:     {args.csv}")
    print(align.summary())
    print(f"{align.path_summary()} in {args.duration:.1f}s")
    print("pan:       arrow keys shift painting vs panel")

    if args.no_window:
        n_frames = max(2, int(round(args.duration * args.fps)))
        frames = [
            upscale(
                crop_to_panel(render_tick(painting, scale, i / (n_frames - 1)), align),
                args.cell,
            )
            for i in range(n_frames)
        ]
        write_gif(frames, args.gif, args.fps)
        print(f"gif:       {args.gif} ({n_frames} frames)")
        return

    frames = run_window(
        painting,
        scale,
        args.duration,
        args.fps,
        args.cell,
        args.loop,
        collect=args.gif is not None,
    )
    if args.gif is not None:
        write_gif(frames, args.gif, args.fps)
        print(f"gif:       {args.gif} ({len(frames)} frames)")


if __name__ == "__main__":
    main()
