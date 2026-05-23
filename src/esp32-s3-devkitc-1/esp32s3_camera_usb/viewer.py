#!/usr/bin/env python3
"""
ESP32-S3 USB Camera Viewer
Reads JPEG frames from ESP32 over USB CDC serial and displays live video.

Usage:
    python viewer.py                    # auto-detect port
    python viewer.py COM5               # Windows
    python viewer.py /dev/ttyACM0       # Linux

Requirements:
    pip install pyserial opencv-python numpy
"""

import sys
import time
import struct
import serial
import serial.tools.list_ports
import numpy as np
import cv2

MAGIC       = bytes([0xFF, 0xAA, 0xBB, 0xFF])
MAX_FRAME   = 500_000   # 500 KB sanity cap
WINDOW_NAME = "ESP32-S3 Camera  [ESC=quit  S=snapshot]"


def find_port() -> str:
    """Auto-detect ESP32-S3 CDC port."""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        desc = (p.description or "").lower()
        if any(k in desc for k in ("esp32", "cp210", "ch340", "cdc", "acm")):
            print(f"[auto] found: {p.device} — {p.description}")
            return p.device
    if ports:
        first = ports[0].device
        print(f"[auto] guessing: {first}")
        return first
    raise RuntimeError("No serial port found. Connect ESP32-S3 and try again.")


def sync_magic(ser: serial.Serial) -> bool:
    """Read byte-by-byte until magic sequence found. Blocks until found."""
    buf = bytearray()
    while True:
        b = ser.read(1)
        if not b:
            continue
        buf.append(b[0])
        if len(buf) > 4:
            buf.pop(0)
        if bytes(buf) == MAGIC:
            return True


def readexactly(ser: serial.Serial, n: int) -> bytes | None:
    """Read exactly n bytes, blocking until done or connection lost."""
    buf = bytearray()
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            return None  # timeout / disconnect
        buf.extend(chunk)
    return bytes(buf)


def read_frame(ser: serial.Serial) -> bytes | None:
    """Read one JPEG frame after magic has been consumed."""
    raw = readexactly(ser, 4)
    if raw is None:
        return None
    length = struct.unpack("<I", raw)[0]
    if length == 0 or length > MAX_FRAME:
        return None
    data = readexactly(ser, length)
    if data is None:
        return None
    # Validate JPEG SOI marker (0xFF 0xD8)
    if len(data) < 2 or data[0] != 0xFF or data[1] != 0xD8:
        return None
    return data


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()

    print(f"Connecting to {port} ...")
    ser = serial.Serial(port, baudrate=115200, timeout=10)
    print("Connected. Waiting for first frame...")

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)

    fps_counter = 0
    fps_display = 0.0
    fps_ts      = time.time()
    snapshot_n  = 0

    while True:
        sync_magic(ser)

        data = read_frame(ser)
        if data is None:
            ser.reset_input_buffer()  # flush garbage on desync
            continue

        arr = np.frombuffer(data, dtype=np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if img is None:
            continue

        # FPS
        fps_counter += 1
        now = time.time()
        elapsed = now - fps_ts
        if elapsed >= 1.0:
            fps_display = fps_counter / elapsed
            fps_counter = 0
            fps_ts = now

        # Overlay
        cv2.putText(img, f"{fps_display:.1f} fps", (8, 24),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 200, 0), 2, cv2.LINE_AA)

        cv2.imshow(WINDOW_NAME, img)

        key = cv2.waitKey(1) & 0xFF
        if key == 27:           # ESC
            break
        elif key == ord('s') or key == ord('S'):
            fname = f"snapshot_{snapshot_n:04d}.jpg"
            cv2.imwrite(fname, img)
            print(f"[snap] saved {fname}")
            snapshot_n += 1

    ser.close()
    cv2.destroyAllWindows()
    print("Bye.")


if __name__ == "__main__":
    main()
