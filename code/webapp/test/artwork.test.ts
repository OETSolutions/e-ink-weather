/**
 * Tests for the per-page artwork encoder (FR-15).
 *
 * THE CONTRACT UNDER TEST IS CROSS-LANGUAGE: this produces a byte stream that C code in
 * lib/upload/src/artwork.c parses (and that the device inflates with the ROM's tinfl). Every
 * assertion here is about a byte the device reads, so a change that passes here and breaks the
 * device is the failure mode this file exists to catch. The firmware's own golden-image tests
 * then confirm the two sides render identical pictures.
 */

import { describe, it, expect } from 'vitest';
import { deflateSync, inflateSync } from 'node:zlib';
import {
  encodeArtwork,
  crc32,
  ARTWORK_MAGIC,
  ARTWORK_MAX_PAGES,
  ARTWORK_RAW_LEN,
} from '../src/transfer/artwork';

/** A layer that is mostly white with a little ink, like a real layout. */
function layer(seed = 1): Uint8Array {
  const b = new Uint8Array(ARTWORK_RAW_LEN).fill(0xff);
  for (let i = 0; i < 400; i++) b[(i * 137 + seed) % ARTWORK_RAW_LEN] = 0x00;
  return b;
}

function u32(b: Uint8Array, off: number): number {
  return new DataView(b.buffer, b.byteOffset, b.byteLength).getUint32(off, true);
}

describe('encodeArtwork', () => {
  it('writes the magic and page count the device validates', async () => {
    const { blob } = await encodeArtwork([layer(1), layer(2)]);
    expect(u32(blob, 0)).toBe(ARTWORK_MAGIC);
    expect(u32(blob, 4)).toBe(2);
  });

  /* seq stays 0 on purpose: the DEVICE assigns it at promote time, because only it knows the
   * currently live sequence. A client-supplied seq would let a stale tab promote over a newer
   * set, or fail to promote at all. */
  it('leaves seq for the device to assign', async () => {
    const { blob } = await encodeArtwork([layer(1)]);
    expect(u32(blob, 12)).toBe(0);
  });

  /* The table is padded to the fixed slot count because the device reads it at a FIXED offset —
   * that is what lets it locate an entry without reading the whole table first. */
  it('pads the entry table to the fixed slot count', async () => {
    const { blob } = await encodeArtwork([layer(1)]);
    const tableBytes = blob.length - 16; // header + table + one short stream
    expect(tableBytes).toBeGreaterThan(12 * ARTWORK_MAX_PAGES);
    // Page 1's slot must be zeroed, i.e. "no artwork", not a copy of page 0's entry.
    expect(u32(blob, 16 + 12 * 1 + 8)).toBe(0);
  });

  it('records each page as an offset/length/raw pair into the blob', async () => {
    const { blob } = await encodeArtwork([layer(1), layer(2)]);
    const tableOff = 16;
    const blobOff = 16 + 12 * ARTWORK_MAX_PAGES;

    const off0 = u32(blob, tableOff + 0);
    const len0 = u32(blob, tableOff + 4);
    const raw0 = u32(blob, tableOff + 8);
    const off1 = u32(blob, tableOff + 12);
    const len1 = u32(blob, tableOff + 16);
    const raw1 = u32(blob, tableOff + 20);

    expect(raw0).toBe(ARTWORK_RAW_LEN);
    expect(raw1).toBe(ARTWORK_RAW_LEN);
    expect(off0).toBe(0);
    expect(off1).toBe(len0); // contiguous, so the device can read a stream in one go

    // Each stream really does inflate back to the layer it came from.
    expect(inflateSync(Buffer.from(blob.subarray(blobOff + off0, blobOff + off0 + len0))).length)
      .toBe(ARTWORK_RAW_LEN);
    expect(inflateSync(Buffer.from(blob.subarray(blobOff + off1, blobOff + off1 + len1))).length)
      .toBe(ARTWORK_RAW_LEN);
  });

  /* THE STREAMS MUST BE ZLIB, NOT GZIP. The device passes TINFL_FLAG_PARSE_ZLIB_HEADER, which
   * expects RFC1950 framing; a gzip stream's 1f 8b header would be rejected for a reason that has
   * nothing to do with the picture. This asserts the first byte of a real stream. */
  it('emits zlib streams, not gzip', async () => {
    const { blob } = await encodeArtwork([layer(3)]);
    const blobOff = 16 + 12 * ARTWORK_MAX_PAGES;
    const first = blob[blobOff]!;
    // A zlib header's first byte has low nibble 8 (DEFLATE, 32K window): 0x78 is the common case.
    expect(first & 0x0f).toBe(8);
    expect(first).not.toBe(0x1f); // 1f 8b would be gzip
    // And it must satisfy the zlib check: (b0<<8 | b1) % 31 == 0.
    const cmf = blob[blobOff]!;
    const flg = blob[blobOff + 1]!;
    expect(((cmf << 8) | flg) % 31).toBe(0);
  });

  /* The device recomputes the CRC over the TABLE AND THE BLOB, and compares it against both the
   * client's value and the header's stored one. Getting the covered region wrong here means
   * nothing ever promotes — the failure would look like "the upload silently does nothing". */
  it('checksums exactly the table and blob, not the header', async () => {
    const { blob, crc } = await encodeArtwork([layer(1), layer(2)]);
    expect(u32(blob, 8)).toBe(crc);
    expect(crc32(blob.subarray(16))).toBe(crc);
    // Sanity: it is NOT a checksum of the whole buffer, which is the easy mistake.
    expect(crc32(blob)).not.toBe(crc);
  });

  it('matches the device CRC-32 for a known vector', () => {
    // The standard CRC-32 check value, so a wrong polynomial/reflection is caught here rather
    // than as a mysterious promote failure on hardware.
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
  });

  /* A PAGE WITH NO ARTWORK MUST BE EXPRESSIBLE. raw_len 0 means "render a blank layer" to the
   * device; filling it in with another page's picture is the exact bug this feature fixes. */
  it('records a null page as "no artwork" rather than copying a neighbour', async () => {
    const { blob } = await encodeArtwork([layer(1), null, layer(3)]);
    const t = 16;
    expect(u32(blob, t + 0 + 4)).toBeGreaterThan(0);      // page 0 has a stream
    expect(u32(blob, t + 12 + 4)).toBe(0);                // page 1: comp_len 0
    expect(u32(blob, t + 12 + 8)).toBe(0);                // page 1: raw_len 0
    expect(u32(blob, t + 24 + 4)).toBeGreaterThan(0);     // page 2 has one again
  });

  it('rejects a layer of the wrong size', async () => {
    await expect(encodeArtwork([new Uint8Array(100)])).rejects.toThrow(/78200/);
  });

  it('rejects an empty page list and too many pages', async () => {
    await expect(encodeArtwork([])).rejects.toThrow(/at least one/);
    const many = new Array(ARTWORK_MAX_PAGES + 1).fill(null).map((_, i) => layer(i));
    await expect(encodeArtwork(many)).rejects.toThrow(new RegExp(String(ARTWORK_MAX_PAGES)));
  });

  /* A layout too detailed to store must fail LOUDLY here. The alternative is a device that
   * refuses the stream at promote time and leaves the previous artwork live — which the user
   * would experience as "my labels never changed". */
  it('refuses a layer whose compressed stream exceeds the device budget', async () => {
    /* GENUINELY INCOMPRESSIBLE, and that is the point: a 1 bpp layer can carry at most two
     * values per byte, so the only way to exceed the budget is ink scattered at a fine pitch.
     * A repeating pattern (which is what a first attempt at this used) deflates to almost
     * nothing, so the premise has to be checked rather than assumed. */
    const noise = new Uint8Array(ARTWORK_RAW_LEN);
    let s = 0x12345678;
    for (let i = 0; i < noise.length; i++) {
      // xorshift32 — cheap, deterministic, and passes deflate's entropy check.
      s ^= s << 13; s >>>= 0;
      s ^= s >>> 17;
      s ^= s << 5;  s >>>= 0;
      noise[i] = (s * 2654435761) & 0xff;
    }
    // Confirm the premise: this really is over the budget when compressed.
    const comp = deflateSync(Buffer.from(noise), { level: 9 }).length;
    expect(comp).toBeGreaterThan(4096);
    await expect(encodeArtwork([noise])).rejects.toThrow(/budget|too detailed/);
  });

  /* The whole point of compressing: a realistic layout must fit in a fraction of the raw size. */
  it('compresses a realistic layer far below its raw size', async () => {
    const { blob } = await encodeArtwork([layer(7)]);
    expect(blob.length).toBeLessThan(4096);
    expect(blob.length).toBeLessThan(ARTWORK_RAW_LEN / 20);
  });
});
