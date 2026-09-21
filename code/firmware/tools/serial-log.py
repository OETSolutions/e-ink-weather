#!/usr/bin/env python3
"""Read the device's serial console for a bounded time, or reset it first.

WHY A SCRIPT AND NOT `pio device monitor` / `cat`: both were tried and both are unreliable here.
`pio device monitor` produced no output at all against this build, and piping the raw device through
`cat`/`tr` died with "Illegal byte sequence" on the non-UTF-8 boot bytes. A plain read with
errors='replace' is what actually works.

Usage:
  serial-log.py [seconds] [--reset]

--reset pulses the auto-reset line so the boot log is captured from the first line, which is where
the WiFi got-IP and any partition lookup failure appear.
"""
import sys
import time

import serial

SECS = 12.0
reset = False
args = [a for a in sys.argv[1:]]
if '--reset' in args:
    reset = True
    args.remove('--reset')
if args:
    SECS = float(args[0])

s = serial.Serial('/dev/cu.usbserial-1121310', 115200, timeout=0.3)
if reset:
    # The ESP32 auto-reset circuit: RTS->EN, DTR->IO0. This is the standard esptool sequence for
    # "boot normally", not the download mode.
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.15)
    s.setRTS(False)
    time.sleep(0.05)

t0 = time.time()
buf = b''
while time.time() - t0 < SECS:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
s.close()

sys.stdout.write(buf.decode('utf-8', 'replace'))
