#!/usr/bin/env python3
"""All-up check that the moon and stars are half as light as before.

Renders a full night (and day) frame through animate_sun_moon_stars.py —
painting, category masks, moon, and stars — and checks the firmware color
constants that drive the panels.

Perceptual half-brightness
--------------------------
Moon and star colors are WS2812B PWM duty cycles. FastLED writes them
without gamma, so the codes are linear light and 255 is the white point.
Perceived lightness is CIE L* (CIE 1976) with Rec.709 relative luminance:

    Y  = 0.2126 R + 0.7152 G + 0.0722 B    (R,G,B in 0..1)
    L* = 116 * Y^(1/3) - 16                 if Y > (6/29)^3
       = (29/3)^3 * Y                       otherwise

Half as bright means L*_new = L*_old / 2. Chromaticity stays put: scale the
linear channels by k = Y(L*/2) / Y(L*) and round to the nearest uint8.
Halving the stored codes would leave about 76% of the original L*, because
lightness compresses bright values.

Previous solid colors: moon (190, 210, 255), star (220, 225, 255).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import animate_sun_moon_stars as preview
from panel_align import Alignment

REPO_ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT_DIR = Path(__file__).resolve().parent / "snapshots"
SEED = 1
T_ZENITH = 0.5

# Colors before this change. L* is measured on these, not on a naive half.
BASELINE_MOON = (190, 210, 255)
BASELINE_STAR = (220, 225, 255)
SUN_RGB = (255, 214, 64)

# Rendered pixels (including the preview's random star scale) stay this
# close to half the previous pixel's L*. 8-bit rounding keeps the real
# ratios inside this band; see the snapshot pair below.
L_RATIO_TOLERANCE = 0.01

FIRMWARE = {
    "src/animate-realtime.cpp": ("moon", "star"),
    "src/animate-sun-moon-stars.cpp": ("moon", "star"),
    "src/animate-sun.cpp": ("moon",),
}

_DELTA = 6.0 / 29.0
_EPS = _DELTA ** 3
_KAPPA = (29.0 / 3.0) ** 3


def relative_luminance(rgb: np.ndarray) -> float:
    channels = np.asarray(rgb, dtype=np.float64) / 255.0
    return float(0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2])


def cie_lstar(rgb: np.ndarray) -> float:
    y = relative_luminance(rgb)
    if y <= _EPS:
        return _KAPPA * y
    return 116.0 * (y ** (1.0 / 3.0)) - 16.0


def luminance_for_lstar(lightness: float) -> float:
    if lightness <= 8.0:
        return lightness / _KAPPA
    return ((lightness + 16.0) / 116.0) ** 3


def half_perceived_lightness(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    """Scale linear RGB so CIE L* is half, then round to uint8."""
    y = relative_luminance(rgb)
    target_y = luminance_for_lstar(cie_lstar(rgb) / 2.0)
    scale = target_y / y
    out = []
    for channel in rgb:
        rounded = int(np.floor(channel * scale + 0.5))
        out.append(min(255, max(0, rounded)))
    return (out[0], out[1], out[2])


def load_png(path: Path) -> np.ndarray:
    image = Image.open(path).convert("RGB")
    return np.asarray(image, dtype=np.uint8)


def render_pair() -> tuple[np.ndarray, np.ndarray]:
    preview.align = Alignment()
    painting = preview.load_painting(preview.DEFAULT_HEADER)
    height, width = painting.shape[:2]
    labels = preview.load_categories(preview.DEFAULT_CSV, height, width)
    visible = preview.body_visible_mask(labels)
    night = preview.render_tick(
        painting, visible, T_ZENITH, True, np.random.default_rng(SEED)
    )
    day = preview.render_tick(
        painting, visible, T_ZENITH, False, np.random.default_rng(SEED)
    )
    return night, day


def parse_firmware_color(text: str, name: str) -> tuple[int, int, int]:
    match = re.search(
        rf"{name}_COLOR\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)",
        text,
    )
    if match is None:
        raise AssertionError(f"missing {name}_COLOR")
    return tuple(int(group) for group in match.groups())  # type: ignore[return-value]


def check_constants(expected_moon: tuple[int, int, int], expected_star: tuple[int, int, int]) -> None:
    got_moon = tuple(int(v) for v in preview.MOON_RGB)
    got_star = tuple(int(v) for v in preview.STAR_RGB)
    got_sun = tuple(int(v) for v in preview.SUN_RGB)
    assert got_moon == expected_moon, (got_moon, expected_moon)
    assert got_star == expected_star, (got_star, expected_star)
    assert got_sun == SUN_RGB, got_sun

    for rel, kinds in FIRMWARE.items():
        text = (REPO_ROOT / rel).read_text()
        assert parse_firmware_color(text, "SUN") == SUN_RGB, rel
        assert parse_firmware_color(text, "MOON") == expected_moon, rel
        if "star" in kinds:
            assert parse_firmware_color(text, "STAR") == expected_star, rel
        else:
            assert "STAR_COLOR" not in text, rel


def check_frames(
    night: np.ndarray,
    day: np.ndarray,
    expected_moon: tuple[int, int, int],
) -> None:
    day_path = SNAPSHOT_DIR / "day_zenith.png"
    night_path = SNAPSHOT_DIR / "night_zenith.png"
    previous_path = SNAPSHOT_DIR / "night_zenith_previous.png"
    for path in (day_path, night_path, previous_path):
        if not path.is_file():
            raise AssertionError(f"missing snapshot {path}")

    assert day.shape == (24, 32, 3), day.shape
    assert night.shape == day.shape
    assert np.array_equal(day, load_png(day_path)), "day frame drifted"
    assert np.array_equal(night, load_png(night_path)), "night frame drifted"

    previous = load_png(previous_path)
    assert previous.shape == night.shape
    changed = np.any(night != previous, axis=-1)
    unchanged = ~changed
    assert np.array_equal(night[unchanged], previous[unchanged])
    assert int(changed.sum()) > 0, "moon and stars did not change"

    ratios = []
    moon_seen = False
    for y, x in zip(*np.where(changed)):
        before = previous[y, x]
        after = night[y, x]
        ratio = cie_lstar(after) / cie_lstar(before)
        ratios.append((int(x), int(y), ratio, tuple(int(v) for v in before), tuple(int(v) for v in after)))
        if tuple(int(v) for v in before) == BASELINE_MOON:
            moon_seen = True
            assert tuple(int(v) for v in after) == expected_moon
        assert abs(ratio - 0.5) <= L_RATIO_TOLERANCE, ratios[-1]
    assert moon_seen, "zenith frame has no full-brightness moon pixel"


def main() -> None:
    expected_moon = half_perceived_lightness(BASELINE_MOON)
    expected_star = half_perceived_lightness(BASELINE_STAR)
    check_constants(expected_moon, expected_star)
    night, day = render_pair()
    check_frames(night, day, expected_moon)

    moon_l = cie_lstar(expected_moon)
    star_l = cie_lstar(expected_star)
    base_moon_l = cie_lstar(BASELINE_MOON)
    base_star_l = cie_lstar(BASELINE_STAR)
    print("night lightness check passed")
    print(
        f"  moon {BASELINE_MOON} L* {base_moon_l:.2f} -> {expected_moon} "
        f"L* {moon_l:.2f} ({moon_l / base_moon_l:.4f})"
    )
    print(
        f"  star {BASELINE_STAR} L* {base_star_l:.2f} -> {expected_star} "
        f"L* {star_l:.2f} ({star_l / base_star_l:.4f})"
    )
    print(f"  sun {SUN_RGB} unchanged; day zenith snapshot matches")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"night lightness check failed: {exc}", file=sys.stderr)
        sys.exit(1)
