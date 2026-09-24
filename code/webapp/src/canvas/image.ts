/**
 * Turning an uploaded picture into ink the panel can show.
 *
 * THE DEVICE CANNOT DECODE IMAGES. It is a 1-bit panel with a 1-bit framebuffer and no image
 * codec, so a JPEG or PNG has to become black-and-white pixels in the BROWSER and travel as part
 * of the page's static artwork. That is also why the picture is dithered rather than thresholded:
 * the panel has no grey, so a mid-tone has to be expressed as a pattern of black and white, and a
 * naive threshold would turn a photograph into a blotchy silhouette.
 *
 * FLOYD-STEINBERG, NOT A FIXED THRESHOLD. Error diffusion is the standard choice for 1-bit output
 * and it is what makes a photo legible at this size; the cost is that the result depends on the
 * exact output geometry, which is why the raster is produced AT THE BOX'S PIXEL SIZE rather than
 * scaled at draw time — resizing a dithered bitmap by nearest-neighbour is what makes a preview
 * disagree with the glass.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO: honour colour. The panel has none, so a red logo becomes a
 * grey tone like any other. Doing better (luminance-weighted, say) is a refinement that would
 * change the picture on the glass, so it lives here as the one place to change rather than being
 * scattered through the editor.
 */

import { PANEL_WIDTH, PANEL_HEIGHT } from './bitmap';

/** The largest picture the app will accept, in pixels. Bounded because the raster is drawn into a
 *  full-panel layer and compressed for a 48 KB artwork slot: a picture is stored, not referenced,
 *  so an enormous upload would be refused at Save. The limit is generous — the panel is 920x680 —
 *  and it exists to give the user a sentence rather than a mysterious storage failure. */
export const MAX_IMAGE_PIXELS = 4_000_000; /* a 2000x2000 picture: well past the panel */

/** A picture already rasterised to 1 bpp ink, ready to blit into a static layer.
 *
 *  INK IS A SET BIT, matching the font atlases (and NOT the framebuffer, where a set bit is
 *  white). That is deliberate: a picture is composited the same way a glyph is — ink stamped onto
 *  the layer, never a white rectangle punched through it — and using the atlas convention keeps
 *  the blit identical to blitMasked() rather than needing a second set of rules.
 *
 *  `pitch` is `ceil(w / 8)`, so a patch is self-contained and carries no padding assumptions. */
export interface StaticImage {
  x: number;
  y: number;
  w: number;
  h: number;
  pitch: number;
  ink: Uint8Array;
}

/** Decode an uploaded file into a drawable image.
 *
 *  `createImageBitmap` is used rather than an `<img>` plus a canvas because it decodes off the main
 *  thread and needs no object URL to revoke — and this runs on every resize, so a leaking object
 *  URL would accumulate. REJECTS with a message for a file the browser cannot decode, which the
 *  caller shows rather than silently leaving the box empty.
 */
export async function decodeImageFile(file: File): Promise<ImageBitmap> {
  let bmp: ImageBitmap;
  try {
    bmp = await createImageBitmap(file);
  } catch {
    throw new Error(`"${file.name}" could not be read as an image. Try a PNG or JPEG.`);
  }
  if (bmp.width * bmp.height > MAX_IMAGE_PIXELS) {
    bmp.close();
    throw new Error(
      `"${file.name}" is ${bmp.width}x${bmp.height}, which is larger than this editor will ` +
      `handle. Resize it to something near the display's 920x680 first.`,
    );
  }
  return bmp;
}

/**
 * Rasterise an image into a 1 bpp ink patch of exactly `w` x `h`, dithered.
 *
 * THE ASPECT RATIO IS THE CALLER'S PROBLEM, and it must be told to the user rather than fixed
 * silently: the box has a shape the user chose, and fitting the picture into it either letterboxes
 * (leaving white bars) or distorts. This draws the WHOLE image into the box, which for a
 * mismatched aspect ratio means the picture is stretched. The editor's hint says so, and
 * fitImageIntoBox() computes the non-distorting alternative.
 *
 * Returns a patch in the ATLAS convention (a set bit is ink), so blitting it is the same operation
 * as blitting a glyph — see StaticImage.
 */
export function rasterizeImage(img: ImageBitmap, w: number, h: number): StaticImage {
  if (w <= 0 || h <= 0) return { x: 0, y: 0, w: 0, h: 0, pitch: 0, ink: new Uint8Array(0) };

  /* Draw the image into an offscreen canvas at the box size, then read the pixels back. A canvas
   * is the only way to get the browser's own scaling (which is what makes a 4000px photo usable in
   * a 300px box without writing a resampler), and `imageSmoothingQuality: 'high'` asks for the
   * good filter — the default can be nearest, which on a photograph looks like noise. */
  const off = document.createElement('canvas');
  off.width = w;
  off.height = h;
  const octx = off.getContext('2d', { willReadFrequently: true });
  if (!octx) throw new Error('the browser refused a 2D canvas for the picture');
  octx.imageSmoothingEnabled = true;
  octx.imageSmoothingQuality = 'high';
  /* A WHITE BACKDROP, so a transparent PNG (a logo, a QR code) composites onto white rather than
   * onto transparent black — which getImageData would report as 0,0,0,0 and this would then read
   * as a fully black box. */
  octx.fillStyle = '#ffffff';
  octx.fillRect(0, 0, w, h);
  octx.drawImage(img, 0, 0, w, h);

  const px = octx.getImageData(0, 0, w, h).data;
  return ditherToPatch(px, w, h);
}

/**
 * Floyd-Steinberg dither RGBA pixels into a 1 bpp ink patch.
 *
 * LUMINANCE IS THE REC. 601 WEIGHTING (0.299/0.587/0.114), which is the standard for perceptual
 * brightness and is what makes a green leaf look lighter than a blue sky of the same saturation —
 * a plain average would invert that and the picture would read wrongly even in black and white.
 *
 * ERROR IS DIFFUSED IN PLACE over a Float32 copy, so the working buffer is `w*h*4` bytes. For the
 * largest box the panel can hold (920x680) that is 2.5 MB, which is fine in a browser and would
 * NOT be on the device — but this never runs there: the device receives only the finished 1 bpp
 * artwork.
 *
 * PURE, so the dithering is host-tested: the pixels in and the ink out are the whole contract, and
 * a test can assert a mid-grey becomes a balanced checker rather than all black or all white.
 */
export function ditherToPatch(rgba: Uint8ClampedArray, w: number, h: number): StaticImage {
  const pitch = (w + 7) >> 3;
  const ink = new Uint8Array(pitch * h);
  const lum = new Float32Array(w * h);
  for (let i = 0, p = 0; i < w * h; i++, p += 4) {
    lum[i] = 0.299 * rgba[p]! + 0.587 * rgba[p + 1]! + 0.114 * rgba[p + 2]!;
  }

  for (let yy = 0; yy < h; yy++) {
    for (let xx = 0; xx < w; xx++) {
      const i = yy * w + xx;
      const old = lum[i]!;
      const black = old < 128;
      const nw = black ? 0 : 255;
      if (black) ink[yy * pitch + (xx >> 3)]! |= 0x80 >> (xx & 7);
      const err = old - nw;
      /* The classic 7/16, 3/16, 5/16, 1/16 distribution: the right neighbour takes most of the
       * error, the row below takes the rest, and the two diagonals get the remainder. */
      const put = (nx: number, ny: number, e: number): void => {
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) return; /* error off the edge is dropped */
        lum[ny * w + nx]! += e;
      };
      put(xx + 1, yy, err * (7 / 16));
      put(xx - 1, yy + 1, err * (3 / 16));
      put(xx, yy + 1, err * (5 / 16));
      put(xx + 1, yy + 1, err * (1 / 16));
    }
  }
  return { x: 0, y: 0, w, h, pitch, ink };
}

/**
 * The largest box, in the panel, that fits `img`'s aspect ratio inside `w` x `h` — the "fit"
 * choice that leaves white bars rather than stretching the picture.
 *
 * CENTRED, because a letterboxed picture pinned to a corner reads as a mistake rather than a
 * choice. Returns a whole-pixel box so the raster cannot land on a half pixel (a fractional y is
 * silently dropped by setPx, which has already caused a preview to lose a widget entirely).
 */
export function fitImageIntoBox(
  imgW: number,
  imgH: number,
  w: number,
  h: number,
): { w: number; h: number } {
  if (imgW <= 0 || imgH <= 0 || w <= 0 || h <= 0) return { w: 0, h: 0 };
  const scale = Math.min(w / imgW, h / imgH);
  return {
    w: Math.max(1, Math.round(imgW * scale)),
    h: Math.max(1, Math.round(imgH * scale)),
  };
}

/** Clamp a placement to the panel and to whole pixels. The device clamps too, but a box the editor
 *  shows must be the box the device stores — the same rule the geometry fields follow. */
export function clampPlacement(x: number, y: number, w: number, h: number): {
  x: number; y: number; w: number; h: number;
} {
  const cx = Math.max(0, Math.min(PANEL_WIDTH, Math.round(x)));
  const cy = Math.max(0, Math.min(PANEL_HEIGHT, Math.round(y)));
  return {
    x: cx,
    y: cy,
    w: Math.max(1, Math.min(PANEL_WIDTH - cx, Math.round(w))),
    h: Math.max(1, Math.min(PANEL_HEIGHT - cy, Math.round(h))),
  };
}
