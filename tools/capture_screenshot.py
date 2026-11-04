#!/usr/bin/env python3
"""Capture a screenshot from the ESP32-S3 Win11 demo over its UART.

The firmware streams a PPM6 image between two markers:

    SCREENSHOT_BEGIN #<n> <width>x<height> P6
    <width*height*3 binary bytes, RGB888>
    SCREENSHOT_END #<n>

This script listens on the serial port, reassembles the payload and writes a
PPM file (optionally also a PNG, if Pillow is installed).

Triggering: the firmware captures on demand when it receives the MQTT command
"screenshot" on the csd topic. Pass --trigger to publish that command after
the port is open (requires mosquitto_pub on PATH), or publish it yourself from
another terminal:

    mosquitto_pub -h suijunqiang.top -t esp32s3/win11/cmd -m screenshot

Note: at 115200 baud a 320x240 RGB888 frame is ~230 KB and takes roughly 20 s
to stream; the LVGL task is busy during the transfer (the UI is frozen).

Usage:
    python3 tools/capture_screenshot.py --trigger \
        -o /tmp/opencode/esp32_screenshot.ppm \
        --png /tmp/opencode/esp32_screenshot.png
"""

import argparse
import fcntl
import os
import re
import struct
import subprocess
import sys
import termios
import time

import serial

BEGIN_RE = re.compile(rb"SCREENSHOT_BEGIN #(\d+) (\d+)x(\d+)")


def open_port(port, baud):
    # Open the TTY file descriptor ourselves so we can force CLOCAL (ignore
    # modem control lines) and deassert DTR/RTS *before* the kernel applies
    # its default line state. The ESP32 auto-reset circuit ties DTR to EN and
    # RTS to GPIO0, so a brief DTR pulse during open() would reset the chip
    # and drop the running UI (and force MQTT to re-init).
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    try:
        attrs = termios.tcgetattr(fd)
        # CLOCAL: do not assert/auto-manage DTR on open.
        # HUPCL off: closing the port must not lower DTR either.
        attrs[2] |= termios.CLOCAL
        attrs[2] &= ~termios.HUPCL
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        # Explicitly clear DTR + RTS via ioctl (no reset, no download mode).
        bits = termios.TIOCM_DTR | termios.TIOCM_RTS
        fcntl.ioctl(fd, termios.TIOCMBIC, struct.pack('I', bits))
    except Exception:
        os.close(fd)
        raise

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 2
    ser.dsrdtr = False
    ser.rtscts = False
    ser.fd = fd
    ser.is_open = True
    ser.name = port
    # pyserial's open() creates these pipes used to abort blocking reads
    # and writes; since we attached a pre-opened fd we must create them.
    ser.pipe_abort_read_r, ser.pipe_abort_read_w = os.pipe()
    ser.pipe_abort_write_r, ser.pipe_abort_write_w = os.pipe()
    ser._reconfigure_port()
    ser.dtr = False
    ser.rts = False
    return ser


def publish_trigger(broker, topic):
    subprocess.run(["mosquitto_pub", "-h", broker, "-t", topic,
                    "-m", "screenshot"], check=True, timeout=15)
    print("published MQTT 'screenshot' command", file=sys.stderr)


def wait_for_begin(ser, deadline, trigger=None, fallback_at=None):
    """Skip log lines until the SCREENSHOT_BEGIN marker shows up.

    When `trigger` is a callable, it is invoked as soon as the MQTT connection
    is confirmed by the log, or at `fallback_at` when the connection line was
    emitted before this script started listening. The fallback may fire before
    the connection comes up, in which case the later connection line triggers
    the command once more.
    """
    published = trigger is None
    published_after_connect = trigger is None

    while time.monotonic() < deadline:
        line = ser.readline()

        if not published_after_connect and line and b"mqtt: connected" in line:
            trigger()
            published = True
            published_after_connect = True

        if not published and fallback_at and time.monotonic() >= fallback_at:
            trigger()
            published = True

        if not line:
            continue

        match = BEGIN_RE.search(line)
        if match:
            index, width, height = (int(g) for g in match.groups())
            return index, width, height
        # Echo the line so the user can see progress / errors.
        sys.stderr.write(line.decode("utf-8", errors="replace"))
    return None


def read_exact(ser, length, deadline):
    data = bytearray()
    while len(data) < length:
        if time.monotonic() >= deadline:
            return None
        chunk = ser.read(min(4096, length - len(data)))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def wait_for_end(ser, deadline):
    while time.monotonic() < deadline:
        line = ser.readline()
        if b"SCREENSHOT_END" in line:
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", default="/dev/ttyACM0",
                        help="serial port (default: /dev/ttyACM0)")
    parser.add_argument("-b", "--baud", type=int, default=115200,
                        help="baud rate (default: 115200, matches the console)")
    parser.add_argument("-o", "--output", default="/tmp/esp32_screenshot.ppm",
                        help="output PPM path")
    parser.add_argument("--png", default=None, help="also write a PNG here")
    parser.add_argument("--timeout", type=float, default=120.0,
                        help="overall timeout in seconds (default: 120)")
    parser.add_argument("--trigger", action="store_true",
                        help="publish the MQTT 'screenshot' command after listening")
    parser.add_argument("--broker", default="suijunqiang.top",
                        help="MQTT broker host used by --trigger")
    parser.add_argument("--topic", default="esp32s3/win11/cmd",
                        help="MQTT command topic used by --trigger")
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout

    ser = open_port(args.port, args.baud)
    print(f"listening on {args.port} @ {args.baud} for SCREENSHOT_BEGIN ...",
          file=sys.stderr)

    trigger = None
    fallback_at = None
    if args.trigger:
        def trigger():
            try:
                publish_trigger(args.broker, args.topic)
            except (OSError, subprocess.SubprocessError) as exc:
                print(f"cannot publish MQTT command: {exc}", file=sys.stderr)

        # If the "mqtt: connected" log line scrolled by before we opened the
        # port, fall back to publishing after a short grace period.
        fallback_at = time.monotonic() + 10.0

    begin = wait_for_begin(ser, deadline, trigger, fallback_at)
    if begin is None:
        print("timed out waiting for SCREENSHOT_BEGIN", file=sys.stderr)
        ser.close()
        return 1

    index, width, height = begin
    payload_len = width * height * 3
    print(f"frame #{index}: {width}x{height}, {payload_len} bytes", file=sys.stderr)

    payload = read_exact(ser, payload_len, deadline)
    if payload is None:
        print("timed out reading the image payload", file=sys.stderr)
        ser.close()
        return 1

    if not wait_for_end(ser, deadline):
        print("warning: SCREENSHOT_END marker not seen", file=sys.stderr)

    ser.close()

    ppm = b"P6\n%d %d\n255\n" % (width, height) + payload
    with open(args.output, "wb") as handle:
        handle.write(ppm)
    print(f"wrote {args.output} ({len(ppm)} bytes)", file=sys.stderr)

    if args.png:
        try:
            from PIL import Image
        except ImportError:
            print("Pillow not installed; skipping PNG conversion", file=sys.stderr)
            return 0

        image = Image.frombytes("RGB", (width, height), payload)
        image.save(args.png)
        print(f"wrote {args.png}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
