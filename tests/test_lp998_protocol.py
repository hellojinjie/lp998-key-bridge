import unittest

from lp998_protocol import (
    BridgeMode,
    KeyCode,
    TouchState,
    decode_touch_report,
    handle_touch_report,
)


def touch_report(flags, x, y):
    return bytes(
        [
            0,
            flags,
            x & 0xFF,
            ((x >> 8) & 0x0F) | ((y & 0x0F) << 4),
            (y >> 4) & 0xFF,
        ]
    )


class Lp998ProtocolTests(unittest.TestCase):
    def test_decodes_touch_report_coordinates(self):
        decoded = decode_touch_report(touch_report(0x03, 500, 350))

        self.assertTrue(decoded.active)
        self.assertEqual(decoded.x, 500)
        self.assertEqual(decoded.y, 350)

    def test_tap_emits_return_on_release(self):
        state = TouchState()

        self.assertIsNone(
            handle_touch_report(state, touch_report(0x03, 500, 400), BridgeMode.ARROWS)
        )
        emitted = handle_touch_report(
            state, touch_report(0x00, 505, 405), BridgeMode.ARROWS
        )

        self.assertEqual(emitted, KeyCode.RETURN)

    def test_swipe_downward_emits_arrow_up_in_arrows_mode(self):
        state = TouchState()

        handle_touch_report(state, touch_report(0x03, 500, 300), BridgeMode.ARROWS)
        emitted = handle_touch_report(
            state, touch_report(0x03, 500, 360), BridgeMode.ARROWS
        )

        self.assertEqual(emitted, KeyCode.UP)

    def test_swipe_downward_emits_page_up_in_pages_mode(self):
        state = TouchState()

        handle_touch_report(state, touch_report(0x03, 500, 300), BridgeMode.PAGES)
        emitted = handle_touch_report(
            state, touch_report(0x03, 500, 360), BridgeMode.PAGES
        )

        self.assertEqual(emitted, KeyCode.PAGE_UP)

    def test_horizontal_swipe_rightward_emits_arrow_left(self):
        state = TouchState()

        handle_touch_report(state, touch_report(0x03, 300, 400), BridgeMode.ARROWS)
        emitted = handle_touch_report(
            state, touch_report(0x03, 360, 400), BridgeMode.ARROWS
        )

        self.assertEqual(emitted, KeyCode.LEFT)


if __name__ == "__main__":
    unittest.main()
