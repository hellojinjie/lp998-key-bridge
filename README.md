# UGREEN-LP998 Key Bridge

Small macOS command-line tools for using `UGREEN-LP998` as a keyboard remote.

`lp998-key-bridge` converts LP998 raw touch reports into real macOS keyboard
events. `lp998-hid-logger` logs raw HID reports for diagnosis.

The default target is:

- Vendor ID: `0x0E05`
- Product ID: `0x0A00`

## Build

```bash
make
```

This creates:

```text
./lp998-hid-logger
./lp998-key-bridge
```

## macOS Permission

If no button reports appear, allow your terminal app in:

```text
System Settings -> Privacy & Security -> Input Monitoring
```

Then quit and reopen the terminal before running the tool again.

If the tool prints this error, it is a macOS permission denial:

```text
Failed to open IOHIDManager: 0xe00002e2
This means macOS denied HID access.
```

Depending on your macOS security settings, you may also need to allow the terminal in:

```text
System Settings -> Privacy & Security -> Accessibility
```

`Input Monitoring` is needed to read LP998 HID reports. `Accessibility` is needed
for `lp998-key-bridge` to synthesize keyboard events.

## Run the Logger

Connect `UGREEN-LP998`, then run:

```bash
./lp998-hid-logger
```

Press each remote button one by one. Output looks like:

```text
12:34:56.789 result=0x00000000 reportId=3 length=3 device="UGREEN-LP998 ..." data=01 e9 00
```

Important fields:

- `reportId`: HID report id.
- `length`: number of bytes in the report.
- `data`: raw bytes in hex.

If a button produces a line, macOS is receiving a raw HID report for it. If a direction button produces no line here, browser JavaScript and Karabiner will not be able to use that button either.

For `UGREEN-LP998`, report `6` is a touchscreen-style report. The logger decodes it as:

```text
decoded=touch flags=0x03 active=yes x=500 y=350
```

Report `3` is a Consumer Control report. For example:

```text
decoded=consumer usage=0x00e9 name=volume_increment
```

Observed LP998 button behavior:

- Up button: touch swipe downward.
- Down button: touch swipe upward.
- Left button: touch swipe rightward.
- Right button: touch swipe leftward.
- Center button: touch tap near `x=500 y=400`.
- One photo button: touch tap near `x=512 y=833`.
- Other photo button: Consumer Control `volume_increment`.

## Diagnostics

Listen to all HID devices:

```bash
./lp998-hid-logger --all
```

Use a different device id:

```bash
./lp998-hid-logger --vid 0x0e05 --pid 0x0a00
```

Show help:

```bash
./lp998-hid-logger --help
```

## Run the Key Bridge

Run:

```bash
./lp998-key-bridge
```

Default mapping:

- Remote up/down/left/right -> `ArrowUp` / `ArrowDown` / `ArrowLeft` / `ArrowRight`
- Center button -> `Return`
- Touch-style photo button -> `Return`
- Consumer Control volume button -> ignored

For presentation-style mapping:

```bash
./lp998-key-bridge --mode pages
```

In `pages` mode:

- Remote up -> `PageUp`
- Remote down -> `PageDown`
- Remote left/right -> arrow keys
- Center/photo tap -> `Return`

Verbose debugging:

```bash
./lp998-key-bridge --verbose
```

The bridge emits swipe keys after the second active touch report. The threshold is
only a small movement guard:

```bash
./lp998-key-bridge --swipe-threshold 60 --verbose
```

The default is `20`. Lower values are more sensitive; higher values are more
conservative. The decision still happens on the second `flags=0x03` report.

Check whether macOS allows this terminal to send synthetic keys:

```bash
./lp998-key-bridge --test-key
```

This sends `Return` once and exits. If nothing happens, grant Accessibility
permission to the terminal app, quit the terminal completely, and try again.

Keep the bridge running while using the remote. Press `Ctrl-C` to stop it.

## Clean

```bash
make clean
```
