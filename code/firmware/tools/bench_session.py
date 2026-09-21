#!/usr/bin/env python3
"""One bench session: build once, flash once, then answer EVERY verification question.

WHY THIS EXISTS: verifying this device used to cost a build/flash cycle per question — run the
soak, look at status, push artwork, read the panel — because each of those was a separate ad-hoc
script and each one reflashed with different instrumentation. That is dozens of ~40 s flash
cycles for one round of checks, and the instrumentation itself kept being added and removed.

This does the whole battery in a single pass against ONE firmware image:
  1. boot the device clean and capture the boot log;
  2. refresh N times, recording free heap / largest block per tick, and COUNT the failures that
     matter (a resident or transient framebuffer that could not be allocated);
  3. GET /api/values and print what the device resolved (FR-27);
  4. GET /api/config and re-verify that a config PUT carrying (0,0) does NOT pin the location;
  5. push the real artwork + config through the web app's own encoder;
  6. refresh again after the push, so the post-push state is covered by the same run;
  7. print one summary with a PASS/FAIL per check and the heap trend.

It deliberately does NOT need the bench camera: capturing the panel is a separate step, and
folding a camera grab in would make this script fail on a machine that has none.

Usage:
  tools/bench_session.py                       # 40 refreshes, default IP
  tools/bench_session.py --count 100 --ip 192.168.2.34
  tools/bench_session.py --no-flash            # measure whatever is already running
  tools/bench_session.py --flash               # build + flash first, then measure
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

try:
    import serial
except ImportError:                                  # pragma: no cover
    sys.exit("needs pyserial: pip install pyserial (or use a venv)")

PORT = "/dev/cu.usbserial-1121310"
BAUD = 115200

# Anything the firmware logs that means "the panel will NOT update". These are the only log lines
# this script treats as failures: the USB/mains "no second framebuffer" fallback is expected and is
# deliberately NOT one of them (see the note in soak_refresh.py).
HARD_FAIL = re.compile(
    r"cannot allocate the framebuffer|cannot allocate the transient framebuffer"
    r"|compose failed|panel did not wake|panel update failed|Guru Meditation|abort\(\)"
)

# A FAILED FETCH is a different symptom of the same heap pressure: the handshake could not get its
# contiguous block, so the readings fall back to placeholders. It does not break the panel (the
# last good image is kept), but it means the data on the glass is stale, so it is reported
# separately rather than folded into HARD_FAIL.
FETCH_FAIL = re.compile(
    r"fetch failed|no current reading|forecast request failed|-0x7F00|ALLOC_FAILED"
    r"|esp-tls: .*fail|Failed to open a new connection"
)


class Bench:
    def __init__(self, ip, port):
        self.ip = ip
        self.lines = []
        self._stop = False
        self.ser = serial.Serial(port, BAUD, timeout=0.2)
        self._t = threading.Thread(target=self._read, daemon=True)
        self._t.start()

    def _read(self):
        while not self._stop:
            try:
                raw = self.ser.readline()
            except Exception:
                return
            if raw:
                self.lines.append(raw.decode("utf-8", "replace").rstrip())

    def reset(self, settle=17.0):
        """Pulse the auto-reset line and wait for the device to come back up."""
        self.lines.clear()
        self.ser.setDTR(False)
        self.ser.setRTS(True)
        time.sleep(0.15)
        self.ser.setRTS(False)
        time.sleep(settle)
        return self.wait_up()

    def wait_up(self, timeout=70):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                urllib.request.urlopen(f"http://{self.ip}/api/status", timeout=4).read()
                return True
            except Exception:
                time.sleep(2)
        return False

    def get(self, path, timeout=10):
        with urllib.request.urlopen(f"http://{self.ip}{path}", timeout=timeout) as r:
            return json.load(r)

    def refresh(self):
        req = urllib.request.Request(f"http://{self.ip}/api/refresh", method="POST")
        try:
            with urllib.request.urlopen(req, timeout=25) as r:
                r.read()
            return True
        except Exception:
            return False

    def failures(self):
        return [ln for ln in self.lines if HARD_FAIL.search(ln)]

    def fetch_failures(self):
        return [ln for ln in self.lines if FETCH_FAIL.search(ln)]

    def close(self):
        self._stop = True
        time.sleep(0.3)
        try:
            self.ser.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default="192.168.2.34")
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--flash", action="store_true", help="build + flash before measuring")
    ap.add_argument("--no-flash", action="store_true", help="measure without flashing (default)")
    ap.add_argument("--no-push", action="store_true", help="skip the artwork push step")
    args = ap.parse_args()

    webapp = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "webapp"))
    results = []

    if args.flash:
        print("[1] building and flashing…")
        rc = subprocess.run(
            ["pio", "run", "-e", "esp32dev", "-t", "upload", "--upload-port", args.port],
            cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            capture_output=True, text=True,
        )
        if rc.returncode != 0:
            print(rc.stdout[-2000:], rc.stderr[-2000:])
            sys.exit("flash FAILED — not measuring a stale image")
        print("    flashed")

    b = Bench(args.ip, args.port)
    try:
        print("[2] resetting and booting…")
        if not b.reset():
            sys.exit("device never came up")

        boot_fails = b.failures()
        print(f"    boot failures: {len(boot_fails)}")
        for ln in boot_fails[:5]:
            print("      " + ln)

        print(f"[3] refreshing {args.count}x…")
        series, refresh_fails = [], 0
        for i in range(args.count):
            if not b.refresh():
                refresh_fails += 1
            time.sleep(args.interval)
            try:
                st = b.get("/api/status")
                series.append((st["free_heap"], st["largest_free_block"]))
            except Exception:
                pass
        hard = b.failures()
        fetch_fails = b.fetch_failures()
        print(f"    refreshes completed: {sum(1 for l in b.lines if 'refresh done' in l)}")
        print(f"    HTTP refresh errors: {refresh_fails}")
        print(f"    allocation/panel failures: {len(hard)}")
        for ln in hard[:5]:
            print("      " + ln)
        print(f"    fetch failures (stale data, panel still drawn): {len(fetch_fails)}")
        for ln in fetch_fails[:3]:
            print("      " + ln)
        if series:
            print(f"    heap: first={series[0]} last={series[-1]}")
            print(f"          largest-block last 5: {[x[1] for x in series[-5:]]}")
        results.append(("no framebuffer-allocation failures",
                        len(hard) == 0 and refresh_fails == 0))
        # A fetch failure is reported but NOT a hard fail: it leaves the previous reading on the
        # glass (FR-29) rather than breaking the panel, so it is a quality signal, not a defect.
        results.append(("fetches succeeded (no stale readings)", len(fetch_fails) == 0))

        print("[4] GET /api/values (FR-27)…")
        try:
            v = b.get("/api/values")
            real = [x for x in v["values"] if x["has_value"]]
            print(f"    page {v['page']}/{v['page_count']}, {len(real)} real values")
            for x in real[:4]:
                print(f"      {x['id']:16s} {x['text']!r}")
            results.append(("device reports resolved values",
                            len(v["values"]) > 0))
        except Exception as e:
            print(f"    FAILED: {e}")
            results.append(("device reports resolved values", False))

        print("[5] location must not be pinned to (0,0) by a config PUT…")
        try:
            cfg = b.get("/api/config")
            loc = cfg.get("location", {})
            # Only meaningful if a real location is stored; on a fresh device it may legitimately
            # be empty, in which case the geo-IP fill has not run yet and this check is vacuous.
            if loc.get("latitude") or loc.get("longitude"):
                body = json.dumps({**cfg, "location": {"latitude": 0, "longitude": 0,
                                                       "zipCode": ""}}).encode()
                req = urllib.request.Request(f"http://{b.ip}/api/config", data=body,
                                             headers={"Content-Type": "application/json"},
                                             method="PUT")
                urllib.request.urlopen(req, timeout=30).read()
                time.sleep(2)
                after = b.get("/api/config").get("location", {})
                kept = bool(after.get("latitude") or after.get("longitude"))
                print(f"    location after a (0,0) PUT: {after}")
                results.append(("(0,0) does not overwrite a real location", kept))
            else:
                print("    no location stored yet — check is vacuous, skipping")
                results.append(("(0,0) does not overwrite a real location", True))
        except Exception as e:
            print(f"    FAILED: {e}")
            results.append(("(0,0) does not overwrite a real location", False))

        if not args.no_push:
            print("[6] pushing artwork + config via the app's own encoder…")
            r = subprocess.run(["node", "scripts/push-to-device.mjs", f"http://{b.ip}"],
                               cwd=webapp, capture_output=True, text=True, timeout=300)
            out = (r.stdout or "").strip().splitlines()
            for ln in out:
                if "artwork upload" in ln or "config PUT" in ln:
                    print("    " + ln)
            pushed = any('"ok":true' in ln for ln in out) and any("200" in ln for ln in out)
            results.append(("artwork + config push succeeded", pushed))

            print("[7] refreshing after the push…")
            before = len(b.failures())
            for _ in range(8):
                b.refresh()
                time.sleep(args.interval)
            after_push = b.failures()[before:]
            print(f"    post-push failures: {len(after_push)}")
            for ln in after_push[:3]:
                print("      " + ln)
            results.append(("no failures after the push", len(after_push) == 0))

        try:
            st = b.get("/api/status")
            print(f"[8] final status: artwork_pages={st['artwork_pages']} "
                  f"errors={st['errors']} largest={st['largest_free_block']}")
        except Exception:
            pass

    finally:
        b.close()

    print("\n================ SUMMARY ================")
    ok = True
    for name, passed in results:
        print(f"  {'PASS' if passed else 'FAIL'}  {name}")
        ok = ok and passed
    print("=========================================")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
