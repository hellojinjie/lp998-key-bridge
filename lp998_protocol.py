from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


class BridgeMode(Enum):
    ARROWS = "arrows"
    PAGES = "pages"


class KeyCode(Enum):
    RETURN = "return"
    LEFT = "left"
    RIGHT = "right"
    DOWN = "down"
    UP = "up"
    PAGE_UP = "page_up"
    PAGE_DOWN = "page_down"


@dataclass(frozen=True)
class TouchReport:
    flags: int
    active: bool
    x: int
    y: int


@dataclass
class TouchState:
    active: bool = False
    start_x: int = 0
    start_y: int = 0
    last_x: int = 0
    last_y: int = 0
    active_reports: int = 0
    emitted: bool = False


def decode_touch_report(report: bytes | bytearray | memoryview) -> TouchReport:
    if len(report) < 5:
        raise ValueError("touch report must contain at least 5 bytes")

    flags = int(report[1])
    x = int(report[2]) | ((int(report[3]) & 0x0F) << 8)
    y = ((int(report[3]) >> 4) & 0x0F) | (int(report[4]) << 4)

    return TouchReport(flags=flags, active=(flags & 0x01) != 0, x=x, y=y)


def key_for_direction(dx: int, dy: int, mode: BridgeMode) -> KeyCode:
    if abs(dx) > abs(dy):
        return KeyCode.LEFT if dx > 0 else KeyCode.RIGHT

    if mode == BridgeMode.PAGES:
        return KeyCode.PAGE_UP if dy > 0 else KeyCode.PAGE_DOWN

    return KeyCode.UP if dy > 0 else KeyCode.DOWN


def handle_touch_report(
    state: TouchState,
    report: bytes | bytearray | memoryview,
    mode: BridgeMode,
    swipe_threshold: int = 20,
) -> KeyCode | None:
    touch = decode_touch_report(report)

    if touch.active and not state.active:
        state.active = True
        state.start_x = touch.x
        state.start_y = touch.y
        state.last_x = touch.x
        state.last_y = touch.y
        state.active_reports = 1
        state.emitted = False
        return None

    if touch.active and state.active:
        dx = touch.x - state.start_x
        dy = touch.y - state.start_y
        state.last_x = touch.x
        state.last_y = touch.y
        state.active_reports += 1

        if (
            not state.emitted
            and state.active_reports >= 2
            and (abs(dx) >= swipe_threshold or abs(dy) >= swipe_threshold)
        ):
            state.emitted = True
            return key_for_direction(dx, dy, mode)

        return None

    if not touch.active and state.active:
        dx = state.last_x - state.start_x
        dy = state.last_y - state.start_y
        already_emitted = state.emitted
        state.active = False
        state.emitted = False

        if already_emitted:
            return None

        if abs(dx) >= swipe_threshold or abs(dy) >= swipe_threshold:
            return key_for_direction(dx, dy, mode)

        return KeyCode.RETURN

    return None
