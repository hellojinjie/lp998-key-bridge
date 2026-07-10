import unittest

from lp998_hid_logger import format_report, split_hidapi_report
from lp998_sendinput import VK_CODES
from lp998_protocol import KeyCode


class Lp998WindowsTests(unittest.TestCase):
    def test_key_codes_map_to_windows_virtual_keys(self):
        self.assertEqual(VK_CODES[KeyCode.RETURN], 0x0D)
        self.assertEqual(VK_CODES[KeyCode.LEFT], 0x25)
        self.assertEqual(VK_CODES[KeyCode.UP], 0x26)
        self.assertEqual(VK_CODES[KeyCode.RIGHT], 0x27)
        self.assertEqual(VK_CODES[KeyCode.DOWN], 0x28)
        self.assertEqual(VK_CODES[KeyCode.PAGE_UP], 0x21)
        self.assertEqual(VK_CODES[KeyCode.PAGE_DOWN], 0x22)

    def test_format_report_includes_report_id_length_and_hex_bytes(self):
        line = format_report(6, bytes([0x00, 0x03, 0xF4, 0xE1, 0x15]))

        self.assertIn("reportId=6", line)
        self.assertIn("length=5", line)
        self.assertIn("data=00 03 f4 e1 15", line)
        self.assertIn("decoded=touch", line)
        self.assertIn("active=yes", line)
        self.assertIn("x=500", line)
        self.assertIn("y=350", line)

    def test_split_hidapi_report_strips_known_report_id_prefix(self):
        report_id, payload = split_hidapi_report(bytes([6, 0x00, 0x03, 0xF4, 0xE1, 0x15]))

        self.assertEqual(report_id, 6)
        self.assertEqual(payload, bytes([0x00, 0x03, 0xF4, 0xE1, 0x15]))

    def test_split_hidapi_report_infers_unprefixed_touch_report(self):
        report_id, payload = split_hidapi_report(bytes([0x00, 0x03, 0xF4, 0xE1, 0x15]))

        self.assertEqual(report_id, 6)
        self.assertEqual(payload, bytes([0x00, 0x03, 0xF4, 0xE1, 0x15]))


if __name__ == "__main__":
    unittest.main()
