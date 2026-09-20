import { describe, it, expect } from 'vitest';
import { chunkBitmap, CHUNK_SIZE } from '../src/transfer/bitmap';
import { FB_BYTES } from '../src/model/canvas-consts';

describe('bitmap chunking (IF-2a)', () => {
  it('splits the exact framebuffer size into chunks that reassemble losslessly', () => {
    const data = new Uint8Array(FB_BYTES);
    for (let i = 0; i < data.length; i++) data[i] = i & 0xff;
    const chunks = chunkBitmap(data, 4096);
    expect(chunks.reduce((n, c) => n + c.length, 0)).toBe(FB_BYTES);
    const joined = new Uint8Array(FB_BYTES);
    let off = 0;
    for (const c of chunks) { joined.set(c, off); off += c.length; }
    expect(joined).toEqual(data);
  });

  it('produces a short final chunk when it does not divide evenly', () => {
    const chunks = chunkBitmap(new Uint8Array(FB_BYTES), 4096);
    expect(chunks.length).toBe(Math.ceil(FB_BYTES / 4096));
    expect(chunks[chunks.length - 1]!.length).toBeLessThanOrEqual(4096);
  });

  /* A WRONG-SIZED BUFFER IS REFUSED, not chunked. A short frame is not a smaller picture — the
   * device would store it and render a corrupt image, which is far worse than a clear error. */
  it('refuses a buffer that is not exactly the framebuffer size', () => {
    expect(() => chunkBitmap(new Uint8Array(FB_BYTES - 1))).toThrow(/78200/);
    expect(() => chunkBitmap(new Uint8Array(FB_BYTES + 1))).toThrow(/78200/);
    expect(() => chunkBitmap(new Uint8Array(0))).toThrow();
  });

  it('refuses a nonsensical chunk size instead of looping forever', () => {
    expect(() => chunkBitmap(new Uint8Array(FB_BYTES), 0)).toThrow();
    expect(() => chunkBitmap(new Uint8Array(FB_BYTES), -1)).toThrow();
    expect(() => chunkBitmap(new Uint8Array(FB_BYTES), NaN)).toThrow();
  });

  it('defaults to the documented chunk size', () => {
    expect(CHUNK_SIZE).toBe(4096);
    expect(chunkBitmap(new Uint8Array(FB_BYTES)).length).toBe(Math.ceil(FB_BYTES / CHUNK_SIZE));
  });
});
