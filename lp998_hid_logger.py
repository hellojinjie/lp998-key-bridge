from __future__ import annotations

import argparse
import sys
import time

from lp998_protocol import decode_touch_report

DEFAULT_VENDOR_ID = 0x0E05
DEFAULT_PRODUCT_ID = 0x0A00


def _load_hid_module():
    try:
        import hid
    except ImportError as exc:
        raise SystemExit(
            "Missing dependency: install hidapi with `python -m pip install hidapi`."
        ) from exc
    return hid


def format_report(report_id: int, data: bytes | bytearray) -> str:
    hex_data = " ".join(f"{byte:02x}" for byte in data)
    line = f"reportId={report_id} length={len(data)} data={hex_data}"

    if report_id == 6 and len(data) >= 5:
        touch = decode_touch_report(data)
        line += (
            f" decoded=touch flags=0x{touch.flags:02x}"
            f" active={'yes' if touch.active else 'no'} x={touch.x} y={touch.y}"
        )
    elif report_id == 3 and len(data) >= 3:
        usage = int(data[1]) | (int(data[2]) << 8)
        line += f" decoded=consumer usage=0x{usage:04x}"

    return line


def open_device(hid, vendor_id: int, product_id: int):
    device = hid.device()
    device.open(vendor_id, product_id)
    return device


def split_hidapi_report(data: bytes) -> tuple[int, bytes]:
    if data and data[0] in (3, 6):
        return data[0], data[1:]
    if len(data) >= 5 and data[1] in (0x00, 0x01, 0x02, 0x03):
        return 6, data
    return 0, data


def read_reports(device):
    while True:
        data = device.read(64, timeout_ms=1000)
        if data:
            yield split_hidapi_report(bytes(data))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Listen for raw HID input reports from UGREEN-LP998 on Windows."
    )
    parser.add_argument("--vid", type=lambda value: int(value, 0), default=DEFAULT_VENDOR_ID)
    parser.add_argument("--pid", type=lambda value: int(value, 0), default=DEFAULT_PRODUCT_ID)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    hid = _load_hid_module()

    print(
        f"LP998 HID logger listening for VendorID=0x{args.vid:04x} "
        f"ProductID=0x{args.pid:04x}."
    )
    print("Press LP998 buttons. Press Ctrl-C to stop.")

    device = open_device(hid, args.vid, args.pid)
    try:
        try:
            for report_id, data in read_reports(device):
                timestamp = time.strftime("%H:%M:%S")
                print(f"{timestamp} {format_report(report_id, data)}", flush=True)
        except KeyboardInterrupt:
            return 0
    finally:
        device.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
