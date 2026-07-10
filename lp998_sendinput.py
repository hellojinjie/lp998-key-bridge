from __future__ import annotations

import ctypes
from ctypes import wintypes

from lp998_protocol import KeyCode


VK_CODES = {
    KeyCode.RETURN: 0x0D,
    KeyCode.PAGE_UP: 0x21,
    KeyCode.PAGE_DOWN: 0x22,
    KeyCode.LEFT: 0x25,
    KeyCode.UP: 0x26,
    KeyCode.RIGHT: 0x27,
    KeyCode.DOWN: 0x28,
}

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(wintypes.ULONG)),
    ]


class INPUT_UNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("union", INPUT_UNION)]


def _keyboard_input(vk_code: int, flags: int) -> INPUT:
    event = INPUT()
    event.type = INPUT_KEYBOARD
    event.union.ki = KEYBDINPUT(vk_code, 0, flags, 0, None)
    return event


def press_key(key_code: KeyCode) -> None:
    vk_code = VK_CODES[key_code]
    events = (INPUT * 2)(
        _keyboard_input(vk_code, 0),
        _keyboard_input(vk_code, KEYEVENTF_KEYUP),
    )
    sent = ctypes.windll.user32.SendInput(2, events, ctypes.sizeof(INPUT))
    if sent != 2:
        raise ctypes.WinError()
