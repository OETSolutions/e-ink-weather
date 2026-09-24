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
 * A PAGE IS A RUN OF INDEPENDENT PER-STRIP STREAMS, NOT ONE STREAM. The device decompresses each
 * strip on its own with no LZ dictionary (see artwork.h for why: a single whole-layer stream needs
 * a 32 KB dictionary whose working set cannot coexist with the layer on the device's fragmented
 * heap). So each page's entry points at ARTWORK_STRIP_COUNT back-to-back strip streams, compressed
 * here independently — a back-reference must never cross a strip boundary, or the dictionary-free
 * decode fails. The strips tile the layer exactly (the strip size divides it), so there is no
 * partial tail.
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

/** Must match artwork.h. The magic was bumped when each strip gained a uint16 length prefix — the
 *  device reads one strip at a time by that length, so a prefixless blob would be decoded as
 *  garbage. The value must stay in lockstep with the firmware. */
export const ARTWORK_MAGIC = 0x4c474150;
export const ARTWORK_MAX_PAGES = 8;
export const ARTWORK_RAW_LEN = FB_BYTES;
/** The per-page compressed budget, mirrored from artwork.h. Sized to hold a whole PICTURE — an
 *  image box bakes into this layer — rather than just a text layout. */
export const ARTWORK_MAX_COMP = 49152;
/** The device's artwork slot size (partitions.csv: artwork_a/artwork_b are 0xC000 each). The whole
 *  encoded set must fit one slot, because the device erases the spare and streams the set into it.
 *  The per-page budget alone cannot express that: eight pages of 48 KB would exceed the slot many
 *  times over, and the device would fail mid-stream instead of the encoder refusing up front. */
export const ARTWORK_SLOT_SIZE = 0xc000;
const ARTWORK_STRIP_COMP_MAX = 65535;
/* The strip geometry, mirrored from artwork.h. 3,910 raw bytes = 34 panel rows (34 x 115-byte
 * pitch); 20 strips tile the 680-row layer exactly. The device decompresses each strip with no
 * dictionary, so every strip MUST be its own zlib stream with no cross-strip back-reference. */
export const ARTWORK_STRIP_RAW = 3910;
export const ARTWORK_STRIP_COUNT = ARTWORK_RAW_LEN / ARTWORK_STRIP_RAW; /* 20 */
const ARTWORK_ENTRY_LEN = 12;
const ARTWORK_HDR_LEN = 16;
/** Bytes of length prefix in front of each strip stream. */
const ARTWORK_STRIP_PREFIX = 2;

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
 * EACH PAGE IS COMPRESSED AS ARTWORK_STRIP_COUNT INDEPENDENT STRIP STREAMS, concatenated. This is
 * a hard requirement, not an optimisation: the device decodes each strip with NO dictionary, so a
 * back-reference that crossed a strip boundary would land outside the strip's output buffer and
 * fail. Compressing the whole layer as one stream would produce exactly that.
 *
 * Throws if a layer is the wrong size, or if a page's total compressed size exceeds the device's
 * per-page budget: a stream the device would refuse is better caught here, with a message, than
 * discovered as a blank panel.
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
    /* Compress strip by strip and concatenate, EACH PREFIXED BY ITS LENGTH. The prefix is what
     * lets the device read one strip at a time from flash instead of staging the whole page — see
     * ARTWORK_MAX_COMP in artwork.h for why that matters once a page holds a picture. A strip is
     * exactly ARTWORK_STRIP_RAW bytes (the layer divides evenly), so the slices tile it with no
     * tail. */
    const parts: Uint8Array[] = [];
    let total = 0;
    for (let k = 0; k < ARTWORK_STRIP_COUNT; k++) {
      const c = await zlibDeflate(l.subarray(k * ARTWORK_STRIP_RAW, (k + 1) * ARTWORK_STRIP_RAW));
      /* A strip that does not fit a uint16 prefix would be read back as a smaller number and the
       * whole page would decode to nonsense. The device asserts the same bound; this is the half
       * that can report it usefully. */
      if (c.length === 0 || c.length > ARTWORK_STRIP_COMP_MAX) {
        throw new Error(
          `page ${i}: strip ${k} compressed to ${c.length} bytes, which does not fit the ` +
          `device's 16-bit strip length. This is a bug, not a layout problem.`,
        );
      }
      const framed = new Uint8Array(ARTWORK_STRIP_PREFIX + c.length);
      framed[0] = c.length & 0xff;
      framed[1] = (c.length >> 8) & 0xff;
      framed.set(c, ARTWORK_STRIP_PREFIX);
      parts.push(framed);
      total += framed.length;
    }
    if (total > ARTWORK_MAX_COMP) {
      throw new Error(
        `page ${i}: compressed layer is ${total} bytes, over the device's ` +
        `${ARTWORK_MAX_COMP}-byte budget. Use a smaller picture in this page's image box, or ` +
        `fewer boxes.`,
      );
    }
    const page = new Uint8Array(total);
    let off = 0;
    for (const p of parts) { page.set(p, off); off += p.length; }
    streams.push(page);
  }

  /* Lay the blob out first, so each entry's offset is known before the table is written. */
  let blobLen = 0;
  for (const s of streams) if (s) blobLen += s.length;

  /* THE WHOLE SET MUST FIT ONE SLOT. The device erases the spare slot and streams the set into it,
   * and the header plus the fixed entry table sit in front of the blob — so the check has to cover
   * all three, not just the blob. Checking here rather than on the device turns "the upload failed
   * half way through" into a sentence the user can act on, and it is reachable: the per-page budget
   * is now large enough for a picture, so two picture-heavy pages can genuinely exceed 48 KB. */
  const total_bytes = ARTWORK_HDR_LEN + ARTWORK_ENTRY_LEN * ARTWORK_MAX_PAGES + blobLen;
  if (total_bytes > ARTWORK_SLOT_SIZE) {
    throw new Error(
      `this layout needs ${total_bytes} bytes of artwork but the display can store ` +
      `${ARTWORK_SLOT_SIZE}. Use smaller pictures, or put the large ones on fewer pages ` +
      `(currently ${layers.length}).`,
    );
  }

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
