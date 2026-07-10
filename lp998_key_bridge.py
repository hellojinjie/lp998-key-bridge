from __future__ import annotations

import argparse
import sys

from lp998_hid_logger import (
    DEFAULT_PRODUCT_ID,
    DEFAULT_VENDOR_ID,
    _load_hid_module,
    open_device,
    read_reports,
)
from lp998_protocol import BridgeMode, TouchState, decode_touch_report, handle_touch_report
from lp998_protocol import KeyCode
from lp998_sendinput import press_key


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bridge UGREEN-LP998 HID touch reports to Windows keyboard events."
    )
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=DEFAULT_VENDOR_ID)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=DEFAULT_PRODUCT_ID)
    parser.add_argument("--mode", choices=[mode.value for mode in BridgeMode], default="arrows")
    parser.add_argument("--swipe-threshold", type=int, default=20)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--test-key", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    mode = BridgeMode(args.mode)

    if args.swipe_threshold < 10 or args.swipe_threshold > 1000:
        print("--swipe-threshold must be between 10 and 1000.", file=sys.stderr)
        return 2

    if args.test_key:
        press_key(KeyCode.RETURN)
        return 0

    hid = _load_hid_module()
    state = TouchState()

    print(
        f"LP998 key bridge listening for VendorID=0x{args.vid:04x} "
        f"ProductID=0x{args.pid:04x} mode={mode.value}."
    )
    print("Press LP998 buttons. Press Ctrl-C to stop.")

    device = open_device(hid, args.vid, args.pid)
    try:
        try:
            for report_id, data in read_reports(device):
                if report_id == 6:
                    if args.verbose:
                        touch = decode_touch_report(data)
                        print(
                            f"touch flags=0x{touch.flags:02x} "
                            f"active={'yes' if touch.active else 'no'} "
                            f"x={touch.x} y={touch.y}"
                        )

                    key_code = handle_touch_report(
                        state,
                        data,
                        mode,
                        swipe_threshold=args.swipe_threshold,
                    )
                    if key_code is not None:
                        press_key(key_code)
                        if args.verbose:
                            print(f"emit key={key_code.value}", flush=True)
        except KeyboardInterrupt:
            return 0
    finally:
        device.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
