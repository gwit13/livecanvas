#!/usr/bin/env python3
"""Preview of src/animate-sun-moon-stars.cpp on the painting grid.

Sun for 10s, then moon for 10s with ~6 random stars per frame, then loop.
Same hemisphere, occlusion, and 32x24 top-center crop as the firmware.
"""

from __future__ import annotations

import argparse
import re
import time
import tkinter as tk
from pathlib import Path

import numpy as np
from PIL import Image, ImageTk

from panel_align import (
    PANEL_HEIGHT,
    PANEL_WIDTH,
    Alignment,
    add_origin_args,
    bind_origin_keys,
    crop_to_panel,
)

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

MASK_CLOUDS = True
MASK_HORIZON = True

align = Alignment()

SUN_RADIUS = 2.0
MOON_RADIUS = SUN_RADIUS * 0.5
BODY_AA = 0.25
STAR_COUNT = 6
SUN_DURATION_S = 10.0
MOON_DURATION_S = 10.0
FPS = 15

SUN_RGB = np.array([255.0, 214.0, 64.0])
MOON_RGB = np.array([190.0, 210.0, 255.0])
STAR_RGB = np.array([220.0, 225.0, 255.0])

CRGB_RE = re.compile(r"CRGB\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)")
WIDTH_RE = re.compile(r"#define\s+PAINTING_WIDTH\s+(\d+)")
HEIGHT_RE = re.compile(r"#define\s+PAINTING_HEIGHT\s+(\d+)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    add_origin_args(parser)
    parser.add_argument("--sun-duration", type=float, default=SUN_DURATION_S)
    parser.add_argument("--moon-duration", type=float, default=MOON_DURATION_S)
    parser.add_argument("--fps", type=float, default=FPS)
    parser.add_argument("--cell", type=int, default=PREVIEW_CELL)
    parser.add_argument(
        "--loop",
        action="store_true",
        help="Replay sun→moon until the window is closed",
    )
    parser.add_argument(
        "--gif",
        type=Path,
        default=None,
        help="Also write an animated GIF (panel crop, preview-scale)",
    )
    parser.add_argument(
        "--no-window",
        action="store_true",
        help="Skip the preview window (use with --gif)",
    )
    parser.add_argument("--seed", type=int, default=None)
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
        unknown = sorted({c for c in cells if c not in LABELS})
        if unknown:
            raise ValueError(
                f"{path}:{line_no}: unknown label(s) {unknown}; "
                f"expected one of {', '.join(LABELS)}"
            )
        rows.append(cells)
    if len(rows) != height:
        raise ValueError(f"{path}: expected {height} rows, got {len(rows)}")
    return np.array(rows)


def body_visible_mask(labels: np.ndarray) -> np.ndarray:
    """True where sun/moon/stars may paint, matching firmware flags."""
    visible = np.ones(labels.shape, dtype=bool)
    if MASK_CLOUDS:
        visible[labels == "near-clouds"] = False
    if MASK_HORIZON:
        visible[labels == "foreground"] = False
        visible[labels == "background"] = False
    return visible


def sky_position(t: float) -> tuple[float, float]:
    return align.sky_position(t)


def splat_body(
    background: np.ndarray,
    row: float,
    col: float,
    visible: np.ndarray,
    radius: float,
    color: np.ndarray,
) -> np.ndarray:
    frame = background.astype(np.float64)
    height, width = frame.shape[:2]
    pad = radius + 1.0
    r0 = max(0, int(np.floor(row - pad)))
    r1 = min(height, int(np.ceil(row + pad)) + 1)
    c0 = max(0, int(np.floor(col - pad)))
    c1 = min(width, int(np.ceil(col + pad)) + 1)
    if r0 >= r1 or c0 >= c1:
        return np.clip(np.round(frame), 0, 255).astype(np.uint8)

    yy, xx = np.mgrid[r0:r1, c0:c1]
    dist = np.hypot(xx - col, yy - row)
    alpha = np.clip((radius - dist) / BODY_AA, 0.0, 1.0)
    alpha = (alpha * visible[r0:r1, c0:c1])[..., np.newaxis]
    patch = frame[r0:r1, c0:c1]
    frame[r0:r1, c0:c1] = patch * (1.0 - alpha) + color * alpha
    return np.clip(np.round(frame), 0, 255).astype(np.uint8)


def add_stars(
    painting: np.ndarray,
    frame: np.ndarray,
    visible: np.ndarray,
    rng: np.random.Generator,
) -> np.ndarray:
    """Light STAR_COUNT random visible cells that the body did not paint."""
    out = frame.copy()
    height, width = out.shape[:2]
    placed = 0
    for _ in range(64):
        if placed >= STAR_COUNT:
            break
        y = int(rng.integers(0, height))
        x = int(rng.integers(0, width))
        if not visible[y, x]:
            continue
        if not align.on_panel(x, y):
            continue
        if not np.array_equal(out[y, x], painting[y, x]):
            continue
        scale = int(rng.integers(90, 256)) / 255.0
        out[y, x] = np.clip(np.round(STAR_RGB * scale), 0, 255).astype(np.uint8)
        placed += 1
    return out


def upscale(grid: np.ndarray, cell: int) -> np.ndarray:
    if cell < 1:
        raise ValueError("--cell must be >= 1")
    return np.repeat(np.repeat(grid, cell, axis=0), cell, axis=1)


def render_tick(
    painting: np.ndarray,
    visible: np.ndarray,
    t: float,
    is_moon: bool,
    rng: np.random.Generator,
) -> np.ndarray:
    row, col = sky_position(t)
    if is_moon:
        frame = splat_body(painting, row, col, visible, MOON_RADIUS, MOON_RGB)
        frame = add_stars(painting, frame, visible, rng)
    else:
        frame = splat_body(painting, row, col, visible, SUN_RADIUS, SUN_RGB)
    return crop_to_panel(frame, align)


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
    visible: np.ndarray,
    sun_duration: float,
    moon_duration: float,
    fps: float,
    cell: int,
    loop: bool,
    collect: bool,
    rng: np.random.Generator,
) -> list[np.ndarray]:
    recorded: list[np.ndarray] = []
    delay_ms = max(1, int(round(1000.0 / fps)))
    cycle = sun_duration + moon_duration
    def set_title() -> None:
        root.title(f"sun moon stars  {align.title_suffix()}")

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
        if loop:
            elapsed = elapsed % cycle if cycle > 0 else 0.0
        else:
            elapsed = min(elapsed, cycle) if cycle > 0 else 0.0
        is_moon = elapsed >= sun_duration
        phase = elapsed - sun_duration if is_moon else elapsed
        dur = moon_duration if is_moon else sun_duration
        t = min(phase / dur, 1.0) if dur > 0 else 1.0
        preview = upscale(render_tick(painting, visible, t, is_moon, rng), cell)
        if state["collect"]:
            recorded.append(preview)
        photo = ImageTk.PhotoImage(Image.fromarray(preview))
        state["photo"] = photo
        label.configure(image=photo)
        done = (not loop) and elapsed >= cycle
        if done:
            root.after(delay_ms, close)
            return
        root.after(delay_ms, tick)

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
    if args.sun_duration <= 0 or args.moon_duration <= 0:
        raise ValueError("durations must be > 0")
    if args.fps <= 0:
        raise ValueError("--fps must be > 0")
    if args.no_window and args.gif is None:
        raise ValueError("--no-window requires --gif")

    global align
    align = Alignment(args.origin_x, args.origin_y)
    painting = load_painting(args.header)
    height, width = painting.shape[:2]
    labels = load_categories(args.csv, height, width)
    visible = body_visible_mask(labels)
    rng = np.random.default_rng(args.seed)
    cycle = args.sun_duration + args.moon_duration
    print(f"grid:      {width} x {height}  panel {PANEL_WIDTH}x{PANEL_HEIGHT}")
    print(f"zones:     {args.csv}")
    print(f"mask:      clouds={MASK_CLOUDS} horizon={MASK_HORIZON}")
    print(align.summary())
    print(align.path_summary())
    print("pan:       arrow keys shift painting vs panel")
    print(
        f"cycle:     sun {args.sun_duration:.1f}s + moon {args.moon_duration:.1f}s "
        f"stars={STAR_COUNT}"
    )

    if args.no_window:
        n_frames = max(2, int(round(cycle * args.fps)))
        frames = []
        for i in range(n_frames):
            elapsed = cycle * i / (n_frames - 1)
            is_moon = elapsed >= args.sun_duration
            phase = elapsed - args.sun_duration if is_moon else elapsed
            dur = args.moon_duration if is_moon else args.sun_duration
            t = min(phase / dur, 1.0)
            frames.append(
                upscale(render_tick(painting, visible, t, is_moon, rng), args.cell)
            )
        write_gif(frames, args.gif, args.fps)
        print(f"gif:       {args.gif} ({n_frames} frames)")
        return

    frames = run_window(
        painting,
        visible,
        args.sun_duration,
        args.moon_duration,
        args.fps,
        args.cell,
        args.loop,
        collect=args.gif is not None,
        rng=rng,
    )
    if args.gif is not None:
        write_gif(frames, args.gif, args.fps)
        print(f"gif:       {args.gif} ({len(frames)} frames)")


if __name__ == "__main__":
    main()
