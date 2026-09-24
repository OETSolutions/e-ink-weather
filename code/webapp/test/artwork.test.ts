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
  ARTWORK_MAX_COMP,
  ARTWORK_STRIP_RAW,
  ARTWORK_STRIP_COUNT,
} from '../src/transfer/artwork';

/** A layer that is mostly white with a little ink, like a real layout. */
function layer(seed = 1): Uint8Array {
  const b = new Uint8Array(ARTWORK_RAW_LEN).fill(0xff);
  for (let i = 0; i < 400; i++) b[(i * 137 + seed) % ARTWORK_RAW_LEN] = 0x00;
  return b;
}

/**
 * Decode a page's stored blob the way the DEVICE does: a run of ARTWORK_STRIP_COUNT independent
 * zlib streams, EACH PRECEDED BY A uint16 LITTLE-ENDIAN LENGTH, inflated one after another with no
 * shared dictionary. The device reads one strip at a time under that prefix (see artwork.h for why
 * the prefix exists), so this walks the blob exactly as artwork_store_load_page_seg() does — a
 * length-prefixed walk, not a search for where a stream happens to end.
 */
function decodeStrips(page: Uint8Array): Uint8Array {
  const out = new Uint8Array(ARTWORK_RAW_LEN);
  let at = 0;
  for (let i = 0; i < ARTWORK_STRIP_COUNT; i++) {
    expect(at + 2, `strip ${i} must have a length prefix`).toBeLessThanOrEqual(page.length);
    const len = page[at]! | (page[at + 1]! << 8);
    expect(len, `strip ${i} must declare a length`).toBeGreaterThan(0);
    at += 2;
    expect(at + len).toBeLessThanOrEqual(page.length);
    const r = inflateSync(Buffer.from(page.subarray(at, at + len)));
    expect(r.length, `strip ${i} must inflate to exactly ${ARTWORK_STRIP_RAW} bytes`)
      .toBe(ARTWORK_STRIP_RAW);
    out.set(r, i * ARTWORK_STRIP_RAW);
    at += len;
  }
  /* No trailing bytes: the page IS the strips and their prefixes, so a leftover byte means the
   * encoder and this walk disagree about the framing. */
  expect(at).toBe(page.length);
  return out;
}

/** A page's strip offsets, for tests that need to inflate one strip on its own. */
function stripOffsets(page: Uint8Array): number[] {
  const offs: number[] = [];
  let at = 0;
  for (let i = 0; i < ARTWORK_STRIP_COUNT; i++) {
    const len = page[at]! | (page[at + 1]! << 8);
    offs.push(at + 2);
    at += 2 + len;
  }
  return offs;
}

/** The length a strip declares, or 0 when the prefix is absent. */
function stripLenAt(page: Uint8Array, at: number): number {
  return page[at]! | (page[at + 1]! << 8);
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
    expect(off1).toBe(len0); // contiguous, so the device can read a page in one go

    // Each page really does decode back to the layer it came from, strip by strip.
    const p0 = decodeStrips(blob.subarray(blobOff + off0, blobOff + off0 + len0));
    expect(p0).toEqual(layer(1));
    const p1 = decodeStrips(blob.subarray(blobOff + off1, blobOff + off1 + len1));
    expect(p1).toEqual(layer(2));
  });

  /* THE STRIPS MUST BE INDEPENDENT. The device decodes each with NO dictionary, so a back-reference
   * that crossed a strip boundary would land outside the strip's output buffer and fail. This
   * decodes each strip from its own start and asserts the bytes equal the source — which only holds
   * if the encoder compressed strips independently rather than as one stream. */
  it('compresses each strip independently, so no back-reference crosses a boundary', async () => {
    const src = layer(11);
    const { blob } = await encodeArtwork([src]);
    const blobOff = 16 + 12 * ARTWORK_MAX_PAGES;
    const pageLen = u32(blob, 16 + 4);
    const page = blob.subarray(blobOff, blobOff + pageLen);

    /* Decode strip k as the DEVICE does: from the strip's own byte offset, with a fresh inflater
     * and an output buffer of exactly one strip. If any strip depended on an earlier one's output,
     * inflating it alone would fail or produce the wrong bytes. */
    const decoded = decodeStrips(page);
    expect(decoded).toEqual(src);

    /* And prove independence directly: every strip, inflated ALONE from its recorded start, yields
     * its own slice. A single whole-layer stream would make every strip but the first fail here. */
    const offs = stripOffsets(page);
    for (let i = 0; i < ARTWORK_STRIP_COUNT; i++) {
      const at = offs[i]!;
      const len = stripLenAt(page, at - 2);
      const lone = inflateSync(Buffer.from(page.subarray(at, at + len)));
      expect(lone.length).toBe(ARTWORK_STRIP_RAW);
      expect(new Uint8Array(lone)).toEqual(src.subarray(i * ARTWORK_STRIP_RAW, (i + 1) * ARTWORK_STRIP_RAW));
    }
  });

  /* EVERY STRIP CARRIES ITS LENGTH, and the device reads the strips BY IT. A prefixless blob (which
   * is what an older encoder produced) would still be valid zlib and still pass every entry check,
   * so without this the incompatibility would show up as garbage on the panel rather than here. */
  it('prefixes every strip with its little-endian compressed length', async () => {
    const src = layer(5);
    const { blob } = await encodeArtwork([src]);
    const blobOff = 16 + 12 * ARTWORK_MAX_PAGES;
    const pageLen = u32(blob, 16 + 4);
    const page = blob.subarray(blobOff, blobOff + pageLen);

    let at = 0;
    for (let i = 0; i < ARTWORK_STRIP_COUNT; i++) {
      const len = stripLenAt(page, at);
      expect(len, `strip ${i} length`).toBeGreaterThan(0);
      /* The declared length must be exactly the bytes the stream needs — inflating just those
       * bytes must succeed and inflating one fewer must not. That is the property the device
       * relies on when it reads a fixed number of bytes rather than searching for a boundary. */
      expect(() => inflateSync(Buffer.from(page.subarray(at + 2, at + 2 + len)))).not.toThrow();
      expect(() => inflateSync(Buffer.from(page.subarray(at + 2, at + 2 + len - 1)))).toThrow();
      at += 2 + len;
    }
    expect(at).toBe(pageLen);
  });

  /* THE STREAMS MUST BE ZLIB, NOT GZIP. The device passes TINFL_FLAG_PARSE_ZLIB_HEADER, which
   * expects RFC1950 framing; a gzip stream's 1f 8b header would be rejected for a reason that has
   * nothing to do with the picture. This asserts the first byte of a real stream — which is now two
   * bytes into the page, past the strip's length prefix. */
  it('emits zlib streams, not gzip', async () => {
    const { blob } = await encodeArtwork([layer(3)]);
    const blobOff = 16 + 12 * ARTWORK_MAX_PAGES;
    const first = blob[blobOff + 2]!;
    // A zlib header's first byte has low nibble 8 (DEFLATE, 32K window): 0x78 is the common case.
    expect(first & 0x0f).toBe(8);
    expect(first).not.toBe(0x1f); // 1f 8b would be gzip
    // And it must satisfy the zlib check: (b0<<8 | b1) % 31 == 0.
    const cmf = blob[blobOff + 2]!;
    const flg = blob[blobOff + 3]!;
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
   * would experience as "my labels never changed".
   *
   * The budget is now large enough for a PICTURE, so the premise that a single page can exceed it
   * has to be built rather than assumed: a full layer of 1-bit noise is the densest page this
   * format can hold, and it is measured here before being asserted against the encoder. */
  it('refuses a layer whose compressed stream exceeds the device budget', async () => {
    /* GENUINELY INCOMPRESSIBLE, and that is the point: a 1 bpp layer can carry at most two
     * values per byte, so the only way to exceed the budget is ink scattered at a fine pitch.
     *
     * THE HIGH BYTE OF THE GENERATOR MATTERS, and this is why the premise is measured rather than
     * assumed: an earlier version of this test multiplied by a constant and took the LOW byte,
     * which correlates across neighbours — deflate found it highly compressible (8.7 KB for a
     * whole layer) and the test passed while asserting nothing. Taking the high byte of a
     * well-mixed state gives the ~1.0 ratio the budget is meant to catch. */
    const noise = new Uint8Array(ARTWORK_RAW_LEN);
    let s = 0x12345678;
    for (let i = 0; i < noise.length; i++) {
      s ^= s << 13; s >>>= 0;
      s ^= s >>> 17;
      s ^= s << 5;  s >>>= 0;
      noise[i] = (s >>> 24) & 0xff;
    }
    // Confirm the premise: this really is over the budget when compressed as the device wants it,
    // strip by strip with a length prefix on each.
    let comp = 0;
    for (let k = 0; k < ARTWORK_STRIP_COUNT; k++) {
      comp += 2 + deflateSync(Buffer.from(noise.subarray(k * ARTWORK_STRIP_RAW, (k + 1) * ARTWORK_STRIP_RAW)), { level: 9 }).length;
    }
    expect(comp).toBeGreaterThan(ARTWORK_MAX_COMP);
    await expect(encodeArtwork([noise])).rejects.toThrow(/budget|smaller picture/);
  });

  /* THE WHOLE SET MUST FIT ONE SLOT, and that is a different limit from the per-page budget: two
   * pages can each be legal and still not fit together. The device erases the spare slot and streams
   * the set into it, so a set the slot cannot hold must be refused HERE, with a sentence, rather
   * than failing half way through the upload on a device that has already erased its spare. */
  it('refuses a set that does not fit the artwork slot even when each page is legal', async () => {
    /* Each page is a PICTURE-SIZED but compressible layer, which is what makes this reachable in
     * real use: a photo-heavy page compresses to a few KB, so the per-page budget never fires and
     * only the set total does. Six such pages exceed the 48 KB slot. */
    const pages: Uint8Array[] = [];
    for (let p = 0; p < 6; p++) {
      const b = new Uint8Array(ARTWORK_RAW_LEN).fill(0xff);
      let s = 0x9e3779b9 ^ (p * 2654435761);
      /* A picture-like texture: gradients and blobs, so it compresses to a few KB — legal on its
       * own, unlike the noise case above. */
      for (let y = 0; y < 680; y++) {
        for (let x = 0; x < 920; x++) {
          const v = 128 + 90 * Math.sin(x / 60) * Math.cos(y / 45);
          s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0;
          if (v + ((s >>> 24) % 40) - 20 < 128) b[y * 115 + (x >> 3)]! &= ~(0x80 >> (x & 7));
        }
      }
      pages.push(b);
    }
    /* Premise: every page is under the per-page budget, so only the total is over. */
    const { blob } = await encodeArtwork(pages.slice(0, 1));
    expect(blob.length).toBeLessThan(ARTWORK_MAX_COMP);
    await expect(encodeArtwork(pages)).rejects.toThrow(/can store|smaller picture/);
  });

  /* The whole point of compressing: a realistic layout must fit in a fraction of the raw size.
   * The per-strip split costs a little (each strip carries its own zlib header, empty-stream
   * terminator AND a 2-byte length), so the bar is generous rather than the exact whole-stream
   * ratio. */
  it('compresses a realistic layer far below its raw size', async () => {
    const { blob } = await encodeArtwork([layer(7)]);
    expect(blob.length).toBeLessThan(4096);
    expect(blob.length).toBeLessThan(ARTWORK_RAW_LEN / 20);
  });
});
