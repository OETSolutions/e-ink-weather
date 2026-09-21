#!/usr/bin/env python3
"""Soak the refresh path: fire N refreshes over HTTP while capturing the serial console.

WHY A SELF-CONTAINED SCRIPT: a background shell firing curls while a foreground reader watches
serial does not survive this harness — the reader is reaped when the surrounding tool call ends,
and the capture comes back empty. Doing both in one process makes the measurement reliable.

It reports, per refresh, the device's largest free block at the moment the resident framebuffer
is re-acquired, and counts how many refreshes failed to get it. That number is the whole point:
the panel cannot draw without a contiguous ~78 KB block, and the failure is silent on the glass
(the previous image simply stays).

Usage:  tools/soak_refresh.py [count] [interval_s] [ip]
"""
import re
import sys
import threading
import time
import urllib.request

import serial

COUNT = int(sys.argv[1]) if len(sys.argv) > 1 else 30
INTERVAL = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
IP = sys.argv[3] if len(sys.argv) > 3 else "192.168.2.34"

s = serial.Serial('/dev/cu.usbserial-1121310', 115200, timeout=0.2)

lines = []
stop = False


def reader():
    while not stop:
        raw = s.readline()
        if raw:
            lines.append(raw.decode('utf-8', 'replace').rstrip())


def refresh():
    try:
        req = urllib.request.Request(f"http://{IP}/api/refresh", method="POST")
        urllib.request.urlopen(req, timeout=8).read()
    except Exception as e:                     # noqa: BLE001 - report and continue the soak
        lines.append(f"!! refresh request failed: {e}")


t = threading.Thread(target=reader, daemon=True)
t.start()

for i in range(COUNT):
    refresh()
    time.sleep(INTERVAL)

stop = True
time.sleep(0.3)
s.close()

hdr = [ln for ln in lines if re.search(r'refresh|artwork|memory|page|heap', ln, re.I)]
print("\n".join(hdr[-COUNT * 4:]))

# The trend is the point: a heap that declines and then PLATEAUS is churn (the allocator
# returning blocks it reuses); one that declines monotonically is a leak that will eventually
# starve the 78 KB framebuffer allocation and stop the panel updating.
enters = [int(m.group(1)) for ln in lines
          for m in [re.search(r'tick ENTER: free (\d+)', ln)] if m]
if enters:
    print("\nresident-heap-at-tick-entry trend (bytes):")
    for i in range(0, len(enters), 10):
        chunk = enters[i:i + 10]
        print(f"  {i:>3}-{i + len(chunk) - 1:<3} "
              + " ".join(str(v) for v in chunk)
              + f"   min {min(chunk)}")
    print(f"  first {enters[0]}  last {enters[-1]}  delta {enters[-1] - enters[0]}")

print(f"\n--- {COUNT} refreshes fired ---")
print("re-acquire failures:", sum(1 for ln in lines if 'out of memory for the framebuffer' in ln))
print("transient-buffer misses (expected on USB):",
      sum(1 for ln in lines if 'no second framebuffer' in ln))
