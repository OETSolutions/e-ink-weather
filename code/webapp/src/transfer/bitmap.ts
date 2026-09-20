/**
 * Static-bitmap transfer (IF-2a).
 *
 * The image is 78,200 bytes and the device has no PSRAM, so it is uploaded in chunks with
 * `offset` and `total` in the query. The device keeps them in a partition and only promotes the
 * new image once the last chunk arrives, so an interrupted upload leaves the previous one live —
 * see lib/upload. That is the behaviour this side must not undermine: a failed transfer is
 * reported as failed and the device is left alone, never told "you have a new image".
 */

import { FB_BYTES } from '../model/canvas-consts';

/** Chunk size. 4 KB matches the device's expectations (IF-2a) and keeps each request body well
 *  inside the HTTP server's buffers. */
export const CHUNK_SIZE = 4096;

/**
 * Split a bitmap into chunks, last one possibly short.
 *
 * Throws unless the input is EXACTLY the framebuffer size. A short or long buffer would be
 * accepted by the chunker and produce an image that the device stores but renders as garbage —
 * a partial frame is not a smaller picture, it is a corrupt one.
 */
export function chunkBitmap(data: Uint8Array, size: number = CHUNK_SIZE): Uint8Array[] {
  if (data.length !== FB_BYTES) {
    throw new Error(`bitmap must be exactly ${FB_BYTES} bytes, got ${data.length}`);
  }
  if (!Number.isFinite(size) || size < 1) {
    throw new Error(`chunk size must be positive, got ${size}`);
  }
  const out: Uint8Array[] = [];
  for (let off = 0; off < data.length; off += size) {
    out.push(data.subarray(off, Math.min(off + size, data.length)));
  }
  return out;
}

export interface UploadProgress {
  sent: number;
  total: number;
}

export interface UploadResult {
  ok: boolean;
  error?: string;
}

export interface UploadOptions {
  /** Base URL of the device, e.g. '' for same-origin. */
  baseUrl?: string;
  /** Bearer token, when the device has auth enabled. */
  token?: string;
  onProgress?: (p: UploadProgress) => void;
  signal?: AbortSignal;
}

/**
 * Upload a bitmap to the device, chunk by chunk.
 *
 * RETRIES A CHUNK, because a wifi blip mid-upload is common on a device across the room and the
 * alternative is making the user start again. A retry re-sends the SAME offset — the device's
 * session is offset-keyed, so resending is idempotent, and skipping ahead on a failure would
 * leave a hole in the image that renders as a band of noise rather than an error.
 */
export async function uploadBitmap(
  data: Uint8Array,
  opts: UploadOptions = {},
): Promise<UploadResult> {
  let chunks: Uint8Array[];
  try {
    chunks = chunkBitmap(data);
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : 'bad bitmap' };
  }

  const base = opts.baseUrl ?? '';
  const headers: Record<string, string> = { 'Content-Type': 'application/octet-stream' };
  if (opts.token) headers.Authorization = `Bearer ${opts.token}`;

  let sent = 0;
  for (const chunk of chunks) {
    let attempt = 0;
    for (;;) {
      attempt++;
      try {
        const r = await fetch(`${base}/api/bitmap?offset=${sent}&total=${FB_BYTES}`, {
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
          /* A 4xx is a bad request, not a flaky link — retrying would send the same bad chunk
           * again. Report it with the device's own message. */
          const text = await r.text().catch(() => '');
          return { ok: false, error: `Device refused chunk at ${sent}: ${text || `HTTP ${r.status}`}` };
        }
        if (attempt >= 4) return { ok: false, error: `Upload failed at byte ${sent} (HTTP ${r.status})` };
      } catch (e) {
        if (attempt >= 4) {
          return {
            ok: false,
            error: `Upload interrupted at byte ${sent}: ${e instanceof Error ? e.message : 'network error'}`,
          };
        }
      }
      /* Back off briefly. Short: the device is on the same LAN, so a link that needs longer
       * than this is better reported than waited on. */
      await new Promise((res) => setTimeout(res, 150 * attempt));
    }
    sent += chunk.length;
    opts.onProgress?.({ sent, total: FB_BYTES });
  }

  return { ok: true };
}

/**
 * Render a bitmap to a PNG data URL for previewing in the browser.
 *
 * Uses the SAME bit convention as the device (1 = white, 0 = black, MSB-first) but scales each
 * pixel to whole bytes. `image-rendering: pixelated` on the consuming <img> keeps it crisp.
 */
export function bitmapToPngDataUrl(data: Uint8Array, width: number, height: number): string {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext('2d');
  if (!ctx) throw new Error('canvas 2d context unavailable');

  const img = ctx.createImageData(width, height);
  const pitch = width / 8;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const byte = data[y * pitch + (x >> 3)] ?? 0xff;
      const white = (byte >> (7 - (x & 7))) & 1;
      const p = (y * width + x) * 4;
      const v = white ? 255 : 0;
      img.data[p] = v;
      img.data[p + 1] = v;
      img.data[p + 2] = v;
      img.data[p + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return canvas.toDataURL('image/png');
}
