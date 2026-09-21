#!/usr/bin/env python3
"""Validate partitions.csv before a flash.

A partition table error is a silent, expensive class of bug: an overlap or an out-of-range
region does not fail the build, it fails at runtime with a partition lookup returning NULL —
on a device with no console, after a flash cycle. This checks the arithmetic the table
cannot check for itself.

Checks:
  - every entry is 4 KB sector aligned;
  - every `app` partition is 64 KB aligned, which the bootloader requires for OTA slots —
    gen_esp32part.py rejects the whole table otherwise, so this is a build failure, not a
    runtime one, but it is far cheaper to catch here than after a 40 s rebuild;
  - no partition overlaps another;
  - the table exactly fills the 4 MB flash (no wasted tail, no overflow past the end);
  - both ping-pong pairs have enough room for the largest set they can ever hold: a bitmap slot
    for the header plus a full 1bpp frame, an artwork slot for the header table plus
    ARTWORK_MAX_PAGES streams of at most ARTWORK_MAX_COMP (see lib/upload/include/artwork.h).
    Two stores over one partition pair is a defect this catches: sharing the pair means each
    upload erases the other's live image, which is silent until it reaches the panel.
"""
import csv
import sys
from pathlib import Path

FLASH_SIZE = 0x400000          # 4 MB, ESP32-WROOM-32D (NFR-2)
SECTOR = 0x1000
# The ESP32 bootloader maps each OTA slot at a 64 KB boundary; gen_esp32part.py enforces
# this with "Partition ota_1 invalid: Offset 0x... is not aligned to 0x10000".
OTA_ALIGN = 0x10000
# Must match bitmap_upload.h BITMAP_UPLOAD_TOTAL and the on-flash header.
BITMAP_SLOT_MIN = 16 + 78200
# Must match artwork.h: sizeof(artwork_hdr_t) + sizeof(artwork_entry_t) * ARTWORK_MAX_PAGES, and
# ARTWORK_MAX_PAGES streams at the ARTWORK_MAX_COMP per-stream cap. Derived rather than measured
# because the device must never accept an upload it cannot store.
ARTWORK_SLOT_MIN = 16 + 12 * 8 + 4096 * 8

TABLE = Path(__file__).resolve().parent.parent / "partitions.csv"


def parse(path):
    rows = []
    with path.open() as fh:
        # Strip comment and blank lines before handing the rest to the CSV reader, so a
        # '#' inside a comment cannot be mistaken for a field.
        lines = [ln for ln in fh if ln.strip() and not ln.lstrip().startswith("#")]
    for row in csv.reader(lines):
        if not row or not row[0].strip():
            continue
        name, ptype, subtype, offset, size = (c.strip() for c in row[:5])
        rows.append({
            "name": name,
            "type": ptype,
            "offset": int(offset, 0),
            "size": int(size, 0),
        })
    return rows


def main():
    rows = parse(TABLE)
    if not rows:
        print("FAIL: no partitions parsed — is the file comment-only?", file=sys.stderr)
        return 1

    errors = []

    for p in rows:
        if p["offset"] % SECTOR:
            errors.append(f"{p['name']}: offset 0x{p['offset']:X} is not {SECTOR}-byte aligned")
        if p["size"] % SECTOR:
            errors.append(f"{p['name']}: size 0x{p['size']:X} is not {SECTOR}-byte aligned")
        if p["size"] == 0:
            errors.append(f"{p['name']}: zero size")
        if p["offset"] + p["size"] > FLASH_SIZE:
            errors.append(
                f"{p['name']}: ends at 0x{p['offset'] + p['size']:X}, "
                f"past the {FLASH_SIZE:#x} end of flash")
        # OTA slots must sit on a 64 KB boundary or the bootloader cannot map them.
        if p["type"] == "app" and p["offset"] % OTA_ALIGN:
            errors.append(
                f"{p['name']}: app offset 0x{p['offset']:X} is not "
                f"{OTA_ALIGN:#x}-aligned (the bootloader requires this for OTA slots)")

    # Sort by offset so adjacency is a simple neighbour comparison.
    ordered = sorted(rows, key=lambda p: p["offset"])
    for a, b in zip(ordered, ordered[1:]):
        a_end = a["offset"] + a["size"]
        if a_end > b["offset"]:
            errors.append(
                f"{a['name']} (0x{a['offset']:X}-0x{a_end:X}) overlaps "
                f"{b['name']} (from 0x{b['offset']:X})")

    last_end = ordered[-1]["offset"] + ordered[-1]["size"]
    if last_end != FLASH_SIZE:
        errors.append(
            f"table ends at 0x{last_end:X}, flash is 0x{FLASH_SIZE:X} "
            f"({FLASH_SIZE - last_end} bytes unused)")

    slots = {p["name"]: p for p in rows if p["name"].startswith("bitmap_")}
    for want in ("bitmap_a", "bitmap_b"):
        p = slots.get(want)
        if p is None:
            errors.append(f"{want}: missing — the atomic-promote pair is required (IF-2a)")
        elif p["size"] < BITMAP_SLOT_MIN:
            errors.append(
                f"{want}: {p['size']} bytes is too small for a "
                f"{BITMAP_SLOT_MIN}-byte header+frame")

    aw = {p["name"]: p for p in rows if p["name"].startswith("artwork_")}
    for want in ("artwork_a", "artwork_b"):
        p = aw.get(want)
        if p is None:
            errors.append(f"{want}: missing — the per-page artwork promote pair is required")
        elif p["size"] < ARTWORK_SLOT_MIN:
            errors.append(
                f"{want}: {p['size']} bytes is too small for the "
                f"{ARTWORK_SLOT_MIN}-byte header table plus {4096 * 8} bytes of streams")

    for e in errors:
        print(f"FAIL: {e}", file=sys.stderr)

    if errors:
        return 1

    total = sum(p["size"] for p in rows)
    used = last_end
    print(f"OK: {len(rows)} partitions, {used:#x} of {FLASH_SIZE:#x} bytes "
          f"({used * 100 // FLASH_SIZE}%), no overlap, none wasted")
    for p in ordered:
        print(f"    {p['name']:<10} 0x{p['offset']:06X}..0x{p['offset'] + p['size']:06X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
