"""Painting-to-panel alignment shared by the sun preview scripts.

The LED panel is 32x24. The painting grid is 36x28. Alignment is the
painting cell that sits on panel LED (0, 0):

    panel(x, y) = painting(x + origin_x, y + origin_y)

Default (2, 0) is top-center. Increase origin_x to look further right on
the painting; increase origin_y to look further down.

Arrow keys in a preview window pan this origin. Firmware knobs are
PAINTING_ORIGIN_X / PAINTING_ORIGIN_Y in the .cpp files.
"""

from __future__ import annotations

import argparse

import numpy as np

PANEL_WIDTH = 32
PANEL_HEIGHT = 24
DEFAULT_ORIGIN_X = 2
DEFAULT_ORIGIN_Y = 0
SET_INSET_RIGHT = 3
HORIZON_ROW = 20.0
SUN_START_COL = 0.0


class Alignment:
    def __init__(
        self,
        origin_x: int = DEFAULT_ORIGIN_X,
        origin_y: int = DEFAULT_ORIGIN_Y,
    ) -> None:
        self.origin_x = int(origin_x)
        self.origin_y = int(origin_y)

    @property
    def end_col(self) -> float:
        return float(self.origin_x + PANEL_WIDTH - 1 - SET_INSET_RIGHT)

    @property
    def start_col(self) -> float:
        return SUN_START_COL

    @property
    def center_col(self) -> float:
        return (self.start_col + self.end_col) / 2.0

    @property
    def arc_radius(self) -> float:
        return (self.end_col - self.start_col) / 2.0

    @property
    def apex_row(self) -> float:
        return HORIZON_ROW - self.arc_radius

    def sky_position(self, t: float) -> tuple[float, float]:
        t = float(np.clip(t, 0.0, 1.0))
        theta = np.pi * (1.0 - t)
        col = self.center_col + self.arc_radius * np.cos(theta)
        row = HORIZON_ROW - self.arc_radius * np.sin(theta)
        return row, col

    def sun_height(self, row: float) -> float:
        if self.arc_radius <= 0:
            return 0.0
        return float(np.clip((HORIZON_ROW - row) / self.arc_radius, 0.0, 1.0))

    def on_panel(self, col: int, row: int) -> bool:
        px = col - self.origin_x
        py = row - self.origin_y
        return 0 <= px < PANEL_WIDTH and 0 <= py < PANEL_HEIGHT

    def summary(self) -> str:
        return (
            f"origin:    painting ({self.origin_x}, {self.origin_y}) "
            f"-> panel (0, 0)   "
            f"window painting "
            f"C{self.origin_x}..{self.origin_x + PANEL_WIDTH - 1} "
            f"R{self.origin_y}..{self.origin_y + PANEL_HEIGHT - 1}"
        )

    def path_summary(self) -> str:
        return (
            f"path:      R{HORIZON_ROW:.0f} C{self.start_col:.0f} -> "
            f"R{self.apex_row:.0f} C{self.center_col:.0f} -> "
            f"R{HORIZON_ROW:.0f} C{self.end_col:.0f}"
        )

    def title_suffix(self) -> str:
        return f"origin x={self.origin_x} y={self.origin_y}"


def add_origin_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--origin-x",
        type=int,
        default=DEFAULT_ORIGIN_X,
        help=(
            "Painting column on panel x=0 (default "
            f"{DEFAULT_ORIGIN_X}; higher looks further right)"
        ),
    )
    parser.add_argument(
        "--origin-y",
        type=int,
        default=DEFAULT_ORIGIN_Y,
        help=(
            "Painting row on panel y=0 (default "
            f"{DEFAULT_ORIGIN_Y}; higher looks further down)"
        ),
    )


def crop_to_panel(grid: np.ndarray, align: Alignment) -> np.ndarray:
    """32x24 window into the painting; off-grid cells are black."""
    ph, pw = PANEL_HEIGHT, PANEL_WIDTH
    h, w = grid.shape[:2]
    if grid.ndim == 3:
        out = np.zeros((ph, pw, grid.shape[2]), dtype=grid.dtype)
    else:
        out = np.zeros((ph, pw), dtype=grid.dtype)
    ox, oy = align.origin_x, align.origin_y
    src_y0, src_y1 = max(oy, 0), min(oy + ph, h)
    src_x0, src_x1 = max(ox, 0), min(ox + pw, w)
    if src_y0 >= src_y1 or src_x0 >= src_x1:
        return out
    dst_y0 = src_y0 - oy
    dst_x0 = src_x0 - ox
    dst_y1 = dst_y0 + (src_y1 - src_y0)
    dst_x1 = dst_x0 + (src_x1 - src_x0)
    out[dst_y0:dst_y1, dst_x0:dst_x1] = grid[src_y0:src_y1, src_x0:src_x1]
    return out


def bind_origin_keys(root, align: Alignment, set_title) -> None:
    """Arrow keys pan the painting under the panel. Prints the new origin."""

    def nudge(dx: int, dy: int):
        def handler(_event=None):
            align.origin_x += dx
            align.origin_y += dy
            print(align.summary(), flush=True)
            print(align.path_summary(), flush=True)
            set_title()

        return handler

    root.bind("<Left>", nudge(-1, 0))
    root.bind("<Right>", nudge(1, 0))
    root.bind("<Up>", nudge(0, -1))
    root.bind("<Down>", nudge(0, 1))
