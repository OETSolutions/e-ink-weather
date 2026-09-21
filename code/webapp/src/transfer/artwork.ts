/**
 * Per-page static artwork transfer (FR-15, IF-2a).
 *
 * WHAT THIS FIXES: the device had ONE static layer and rotated pages, so a page's readings were
 * stamped onto a different page's artwork — page 2's "TOMORROW HIGH" label sitting over page 1's
 * temperature. Seen on the glass. The device now holds one artwork layer PER PAGE, and this is
 * what sends them.
 *
 * WHY COMPRESSED: a 1 bpp 920x680 layer is 78,200 bytes raw, so three would fill the device's
 * whole flash. A layout is nearly all white and measures 966 bytes as a zlib stream — 81x — so a
 * set of pages fits comfortably in the two raw partitions that already exist. The device inflates
 * with the ESP32 ROM's tinfl, which costs the firmware no flash.
 *
 * THE FORMAT, and it must match lib/upload/include/artwork.h byte for byte:
 *
 *     artwork_hdr_t     magic, page_count, crc, seq        (16 bytes)
 *     artwork_entry_t   offset, comp_len, raw_len          (12 bytes x 8 slots, fixed)
 *     blob              the concatenated zlib streams
 *
 * The table is padded to ARTWORK_MAX_PAGES entries because the device reads it at a FIXED offset
 * — an entry can then be located without reading the whole table first.
 *
 * THE STREAMS ARE ZLIB (RFC1950), NOT GZIP. The device passes TINFL_FLAG_PARSE_ZLIB_HEADER, which
 * expects a zlib header; a gzip stream's 1f 8b framing would be rejected for a reason unrelated to
 * the picture. So this uses `deflate`, never `gzip`.
 */

import { FB_BYTES } from '../model/canvas-consts';

/**
 * Compress to a zlib (RFC1950) stream.
 *
 * WHY THE WEB COMPRESSIONSTREAM AND NOT NODE'S zlib: this module is bundled into the config app
 * that the DEVICE serves from its own flash and the browser runs — there is no Node there, and
 * `import { deflate } from 'node:zlib'` fails the bundle outright ("promisify is not exported by
 * __vite-browser-external"). CompressionStream is the platform's own deflate, present in every
 * target browser and in Node 18+, and `'deflate'` is the zlib format — the same bytes Node's
 * deflate() produced, which is what the ROM's tinfl wants. (`'deflate-raw'` would drop the
 * RFC1950 header and the device would reject it; `'gzip'` frames it as 1f 8b and would too.)
 *
 * Node's deflate defaulted level 6 and the encoder asked for 9; CompressionStream does not
 * expose a level. That is not a format difference — any valid zlib stream inflates identically —
 * only a size one, and the measured gap is a few percent against a budget with 4x headroom. */
async function zlibDeflate(data: Uint8Array): Promise<Uint8Array> {
  const cs = new CompressionStream('deflate');
  const writer = cs.writable.getWriter();
  /* Copied into a fresh ArrayBuffer-backed view: a Uint8Array may be backed by a
   * SharedArrayBuffer, which the stream writer's BufferSource type does not accept. */
  void writer.write(new Uint8Array(data));
  void writer.close();
  const buf = await new Response(cs.readable).arrayBuffer();
  return new Uint8Array(buf);
}

/** Must match artwork.h. */
export const ARTWORK_MAGIC = 0x50474554;
export const ARTWORK_MAX_PAGES = 8;
export const ARTWORK_RAW_LEN = FB_BYTES;
export const ARTWORK_MAX_COMP = 4096;
const ARTWORK_ENTRY_LEN = 12;
const ARTWORK_HDR_LEN = 16;

/** CRC-32 (IEEE), the same value the device's artwork_crc32() produces. */
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[i] = c >>> 0;
  }
  return t;
})();

export function crc32(data: Uint8Array): number {
  let c = 0xffffffff;
  for (const b of data) c = CRC_TABLE[(c ^ b) & 0xff]! ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function writeU32(view: DataView, off: number, v: number): void {
  view.setUint32(off, v >>> 0, true); /* little-endian: matches the C struct on ESP32 */
}

/**
 * Encode a set of page layers into the device's artwork format.
 *
 * `layers[i]` is page i's 78,200-byte 1 bpp bitmap, or null for a page with no artwork. A null is
 * written as an entry with raw_len 0, which the device reads as "render a blank layer" — NOT as
 * "borrow another page's". That distinction is the whole point of the feature, so a missing page
 * must be expressible rather than filled in.
 *
 * Throws if a layer is the wrong size, or if a compressed stream exceeds the device's per-page
 * budget: a stream the device would refuse is better caught here, with a message, than discovered
 * as a blank panel.
 */
export async function encodeArtwork(
  layers: (Uint8Array | null)[],
): Promise<{ blob: Uint8Array; crc: number; pageCount: number }> {
  if (layers.length === 0) throw new Error('artwork needs at least one page');
  if (layers.length > ARTWORK_MAX_PAGES) {
    throw new Error(
      `artwork supports ${ARTWORK_MAX_PAGES} pages; got ${layers.length}. ` +
      `Reduce the number of pages before pushing.`,
    );
  }

  const streams: (Uint8Array | null)[] = [];
  for (let i = 0; i < layers.length; i++) {
    const l = layers[i];
    if (!l) { streams.push(null); continue; }
    if (l.length !== ARTWORK_RAW_LEN) {
      throw new Error(`page ${i}: layer must be ${ARTWORK_RAW_LEN} bytes, got ${l.length}`);
    }
    const comp = await zlibDeflate(l);
    if (comp.length > ARTWORK_MAX_COMP) {
      throw new Error(
        `page ${i}: compressed layer is ${comp.length} bytes, over the device's ` +
        `${ARTWORK_MAX_COMP}-byte budget. The layout is too detailed to store.`,
      );
    }
    streams.push(comp);
  }

  /* Lay the blob out first, so each entry's offset is known before the table is written. */
  let blobLen = 0;
  for (const s of streams) if (s) blobLen += s.length;
  const blob = new Uint8Array(blobLen);
  const entries = new Uint8Array(ARTWORK_ENTRY_LEN * ARTWORK_MAX_PAGES); /* padded */
  const ev = new DataView(entries.buffer);
  let off = 0;
  for (let i = 0; i < streams.length; i++) {
    const s = streams[i];
    if (!s) {
      /* raw_len 0 = "this page has no artwork". comp_len and offset stay 0 too. */
      continue;
    }
    blob.set(s, off);
    writeU32(ev, i * ARTWORK_ENTRY_LEN + 0, off);
    writeU32(ev, i * ARTWORK_ENTRY_LEN + 4, s.length);
    writeU32(ev, i * ARTWORK_ENTRY_LEN + 8, ARTWORK_RAW_LEN);
    off += s.length;
  }

  /* The header the DEVICE validates against. `seq` is deliberately 0: the device assigns the
   * real sequence at promote time, because it is the one that knows the currently live sequence
   * — sending it from here would let a stale client promote over a newer set. */
  const hdr = new Uint8Array(ARTWORK_HDR_LEN);
  const hv = new DataView(hdr.buffer);
  writeU32(hv, 0, ARTWORK_MAGIC);
  writeU32(hv, 4, streams.length);
  writeU32(hv, 8, 0);      /* crc: filled in below, once it can be computed */
  writeU32(hv, 12, 0);     /* seq: assigned by the device */

  const out = new Uint8Array(hdr.length + entries.length + blob.length);
  out.set(hdr, 0);
  out.set(entries, hdr.length);
  out.set(blob, hdr.length + entries.length);

  /* CRC over the TABLE and the BLOB — matching artwork_make_hdr(), which covers exactly those two
   * regions and not the header. The device recomputes it over what landed in flash and requires
   * it to match BOTH this value and the one in the header.
   *
   * WRITTEN THROUGH A VIEW ON `out`, NOT ON `hdr`. Writing into the header's own buffer after it
   * has been copied into `out` leaves the value in the copy that is actually sent — which is a
   * zero CRC, and the device rejects the upload as a checksum mismatch while the code looks
   * correct. That mistake was in the first version of this function and is what the
   * "checksums exactly the table and blob" test caught. */
  const crc = crc32(out.subarray(ARTWORK_HDR_LEN));
  writeU32(new DataView(out.buffer, out.byteOffset, out.byteLength), 8, crc);

  return { blob: out, crc, pageCount: streams.length };
}

export interface ArtworkUploadResult {
  ok: boolean;
  error?: string;
  pages?: number;
}

/** Chunk size for the artwork upload; 4 KB matches the device's per-request cap. */
const CHUNK = 4096;

/**
 * Upload a set of page layers to the device.
 *
 * RETRIES A CHUNK for the same reason the bitmap upload does — a wifi blip across the room is
 * common and the alternative is starting a whole set again. A retry re-sends the SAME offset; the
 * device's session is offset-keyed, so resending is idempotent, while skipping ahead would leave
 * a hole that the entry table then points into, producing a stream that is half of one picture.
 */
export async function uploadArtwork(
  layers: (Uint8Array | null)[],
  opts: { baseUrl?: string; token?: string; signal?: AbortSignal } = {},
): Promise<ArtworkUploadResult> {
  let encoded: { blob: Uint8Array; crc: number; pageCount: number };
  try {
    encoded = await encodeArtwork(layers);
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : 'could not encode the artwork' };
  }

  const base = opts.baseUrl ?? '';
  const headers: Record<string, string> = { 'Content-Type': 'application/octet-stream' };
  if (opts.token) headers.Authorization = `Bearer ${opts.token}`;

  const { blob, crc, pageCount } = encoded;
  let sent = 0;
  for (let off = 0; off < blob.length; off += CHUNK) {
    const chunk = blob.subarray(off, Math.min(off + CHUNK, blob.length));
    /* The checksum rides on the chunk that COMPLETES the total, exactly as the bitmap upload
     * does. A separate zero-length "commit" request would be refused: the device rejects an empty
     * body outright, because an empty chunk in a real upload is a bug it wants to hear about
     * rather than a commit signal. */
    const isLast = sent + chunk.length >= blob.length;
    let attempt = 0;
    for (;;) {
      attempt++;
      try {
        const q = `offset=${sent}&total=${blob.length}` + (isLast ? `&crc=${crc}` : '');
        const r = await fetch(`${base}/api/artwork?${q}`, {
          method: 'POST',
          headers,
          body: chunk as unknown as BodyInit,
          signal: opts.signal,
        });
        if (r.ok) break;
        if (r.status === 401) {
          return { ok: false, error: 'The device rejected the upload: authentication required.' };
        }
        if (r.status >= 400 && r.status < 500) {
          /* A 4xx is a bad request, not a flaky link — retrying sends the same bad chunk again. */
          const text = await r.text().catch(() => '');
          return { ok: false, error: `Device refused artwork chunk at ${sent}: ${text || `HTTP ${r.status}`}` };
        }
        if (attempt >= 4) return { ok: false, error: `Artwork upload failed at byte ${sent} (HTTP ${r.status})` };
      } catch (e) {
        if (attempt >= 4) {
          return {
            ok: false,
            error: `Artwork upload interrupted at byte ${sent}: ${e instanceof Error ? e.message : 'network error'}`,
          };
        }
      }
      await new Promise((res) => setTimeout(res, 150 * attempt));
    }
    sent += chunk.length;
  }

  return { ok: true, pages: pageCount };
}
