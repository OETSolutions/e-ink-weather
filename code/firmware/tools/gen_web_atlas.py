#!/usr/bin/env python3
"""Convert the firmware's generated font atlas into a TypeScript module.

WHY GENERATE RATHER THAN RE-RASTERISE: the web app's renderer must produce the SAME BITS as
the firmware, or the live preview on the config page lies about what the panel will show
(NFR-4). Re-running Pillow here would be a second, independent rasteriser — two chances to
disagree about hinting or thresholding, with the golden-image test as the only thing that
would notice. The firmware's atlas header is already the single source of truth, so this
reads it instead of duplicating the pipeline.

The output is committed (webapp/src/canvas/atlas-data.ts) so a normal `npm test` needs no
C source and no Pillow. Re-run this only when the atlas changes:

    python3 firmware/tools/gen_web_atlas.py \
        firmware/lib/layout/include/atlas_ladder.h firmware/lib/layout/src/atlas.h \
        webapp/src/canvas/atlas-data.ts

WHY TWO HEADERS: the counts and the ladder come from atlas_ladder.h (the small, widely-included
shape header), and the bitmaps and metrics from atlas.h (the bulky data header only fonts.c
includes). Reading the shape from the same file the C enum is built from is what keeps the two
trees from disagreeing about which sizes exist.

WHAT IS COPIED: glyph metrics (width, height, advance, bearings) VERBATIM, and the packed
1 bpp bitmaps byte-for-byte. The only change is the container.

BIT ORDER IS DELIBERATELY LEFT AS-IS. In the atlas a SET bit is ink; in the panel framebuffer
a CLEAR bit is black (HW-6). Inverting here would make the two conventions match in the data
and diverge in the renderer's head — the inversion belongs in exactly one place, and that
place is the blitter, where the canvas convention is stated.

WHY THE SIZES ARE EMITTED AS A NAMED LADDER rather than as FONT_BODY / FONT_VALUE: the atlas
is now a ladder (see gen_font_atlas.py for why), and the web app has to offer a user exactly
the sizes the panel can actually draw. The names come from the header so the two trees cannot
disagree about what exists; a face added there appears here on the next run, with no edit.
"""
import base64
import re
import sys


def parse_header(ladder_path, data_path):
    shape = open(ladder_path, encoding='utf-8').read()
    src = open(data_path, encoding='utf-8').read()

    def define(name):
        m = re.search(rf'#define\s+{name}\s+(\d+)', shape)
        if not m:
            sys.exit(f'{ladder_path}: missing #define {name}')
        return int(m.group(1))

    face_count = define('ATLAS_FACE_COUNT')
    first_char = define('ATLAS_FIRST_CHAR')
    glyph_count = define('ATLAS_GLYPH_COUNT')

    # The ladder, straight from the X-macro list the C enum is built from — so the sizes and
    # their order are the same on both sides by construction.
    lm = re.search(r'#define\s+ATLAS_LADDER\(X\)(.*?)\n\s*/\*\s*end\s*\*/', shape, re.S)
    if not lm:
        sys.exit(f'{ladder_path}: ATLAS_LADDER list not found')
    px_list = [int(m.group(1)) for m in re.finditer(r'X\((\d+)\)', lm.group(1))]
    if len(px_list) != face_count:
        sys.exit(f'{ladder_path}: ATLAS_LADDER has {len(px_list)} entries, '
                 f'ATLAS_FACE_COUNT is {face_count}')

    faces = []
    for px in px_list:
        v = f'A{px}'

        # The face struct's metrics, so the web app's line height and baseline cannot drift
        # from the device's. Row shape: { px, ascent, descent, lineHeight, GLYPHS, BITS, EXTRA,
        # extraCount, upscale, upscaleOf }. `upscale` is 1 for a rasterised face and k for one
        # drawn as a k x k BLOCK SCALE of `upscaleOf` (see gen_font_atlas.py); an upscaled face
        # points its glyphs/bits at its base's arrays, so this generator must reuse them too
        # rather than re-emit the bitmaps.
        row = re.search(
            rf'^\s*\{{\s*{px},\s*(\d+),\s*(\d+),\s*(\d+)\s*,.*?,\s*(\d+),\s*(\d+)\s*\}},',
            src, re.M)
        if not row:
            sys.exit(f'{data_path}: no ATLAS_FACES row for {px}px')
        ascent, descent, line_height = (int(g) for g in row.groups()[:3])
        upscale, upscale_of = int(row.group(4)), int(row.group(5))

        # An upscaled face owns no arrays of its own: it borrows its base's glyph table and
        # bitmap. Look those up rather than re-reading (and re-emitting) them.
        src_px = upscale_of if upscale > 1 else px
        sv = f'A{src_px}'

        def arr_of(v, kind):
            m = re.search(rf'{v}_{kind}\[[^\]]+\]\s*=\s*\{{(.*?)\n\}};', src, re.S)
            if not m:
                sys.exit(f'{data_path}: {v}_{kind} not found')
            return m.group(1)

        glyphs = []
        for g in re.finditer(r'\{\s*(\d+)u?,\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                             arr_of(sv, 'GLYPHS')):
            glyphs.append(tuple(int(x) for x in g.groups()))
        if len(glyphs) != glyph_count:
            sys.exit(f'{data_path}: {sv} parsed {len(glyphs)} glyphs, ATLAS_GLYPH_COUNT is '
                     f'{glyph_count}')

        extra = []
        em = re.search(rf'{sv}_EXTRA\[[^\]]+\]\s*=\s*\{{(.*?)\n\}};', src, re.S)
        if em:
            for g in re.finditer(
                    r'\{\s*(\d+)u?,\s*(\d+)u?,\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}',
                    em.group(1)):
                extra.append(tuple(int(x) for x in g.groups()))

        bits = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', arr_of(sv, 'BITS'))]

        faces.append({
            'px': px, 'ascent': ascent, 'descent': descent, 'lineHeight': line_height,
            'first': first_char, 'glyphs': glyphs, 'extra': extra, 'bits': bits,
            'upscale': upscale, 'upscaleOf': upscale_of, 'srcPx': src_px,
        })

    return faces


def emit(faces, out_path):
    L = []
    L.append('/* GENERATED by firmware/tools/gen_web_atlas.py — DO NOT EDIT.')
    L.append(' *')
    L.append(" * The firmware's own font atlas, as TypeScript. Copied verbatim so the web")
    L.append(' * renderer cannot disagree with the device about a glyph (NFR-4); see the')
    L.append(' * generator for why this is a conversion and not a second rasteriser.')
    L.append(' *')
    L.append(' * Atlas bit convention: a SET bit is INK. The framebuffer convention is the')
    L.append(' * opposite (a CLEAR bit is black, HW-6) — the inversion lives in the blitter. */')
    L.append('')
    L.append('export interface Glyph {')
    L.append("  /** Offset into the face's bits array. */")
    L.append('  off: number;')
    L.append('  w: number;')
    L.append('  h: number;')
    L.append('  advance: number;')
    L.append('  /** Pen-origin to ink-box offset. by is from the BASELINE and negative above it. */')
    L.append('  bx: number;')
    L.append('  by: number;')
    L.append('}')
    L.append('')
    L.append('export interface Face {')
    L.append('  firstChar: number;')
    L.append('  /** Non-ASCII glyphs, keyed by codepoint. Empty when the face has none. */')
    L.append('  extra: Map<number, Glyph>;')
    L.append('  px: number;')
    L.append('  ascent: number;')
    L.append('  descent: number;')
    L.append('  lineHeight: number;')
    L.append('  glyphs: Glyph[];')
    L.append('  bits: Uint8Array;')
    L.append('  /**')
    L.append('   * The block-scale factor: 1 for a rasterised face, k >= 2 for one drawn as a k x k')
    L.append('   * block scale of `upscaleFromPx`. Mirrors the firmware\'s atlas_face_t.upscale; the')
    L.append('   * web renderer applies it exactly as the device does (NFR-4). The metrics above are')
    L.append('   * the BASE\'s, unscaled, so a glyph\'s w/h matches the bitmap the face points at.')
    L.append('   */')
    L.append('  upscale: number;')
    L.append('  /** The px size of the face this one scales, or this face\'s own size when upscale is 1. */')
    L.append('  upscaleFromPx: number;')
    L.append('}')
    L.append('')

    # Only RASTERISED faces own glyph tables and bitmaps. An upscaled face points its Face at
    # its base's — re-emitting the same bytes for a 256px face would double the bundle for no
    # reason, which is the whole economy the block-scale design buys.
    for f in faces:
        if f['upscale'] != 1:
            continue
        v = f'A{f["px"]}'
        L.append(f'const {v}_GLYPHS: Glyph[] = [')
        for (off, w, h, adv, bx, by) in f['glyphs']:
            L.append(f'  {{ off: {off}, w: {w}, h: {h}, advance: {adv}, bx: {bx}, by: {by} }},')
        L.append('];')
        L.append('')
        # A base64 string, decoded at load: a 68,000-number array literal is ~400 KB of source
        # the bundler must parse at startup, versus one string decode.
        b64 = base64.b64encode(bytes(f['bits'])).decode('ascii')
        chunks = [b64[i:i + 96] for i in range(0, len(b64), 96)]
        L.append(f'/* {len(f["bits"])} bytes of packed {f["px"]}px glyph data. */')
        L.append(f'const {v}_B64 =')
        for i, c in enumerate(chunks):
            end = ';' if i == len(chunks) - 1 else ' +'
            L.append(f"  '{c}'{end}")
        L.append('')
        L.append(f'const {v}_BITS = b64decode({v}_B64);')
        L.append('')

    L.append('/** Decode a base64 string to bytes without a DOM or Buffer dependency. */')
    L.append('function b64decode(s: string): Uint8Array {')
    L.append("  const B = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';")
    L.append('  const out = new Uint8Array((s.length * 3) >> 2);')
    L.append('  let o = 0, buf = 0, bits = 0;')
    L.append('  for (let i = 0; i < s.length; i++) {')
    L.append("    const ch = s[i]!;")
    L.append("    if (ch === '=') break;")
    L.append('    buf = (buf << 6) | B.indexOf(ch);')
    L.append('    bits += 6;')
    L.append('    if (bits >= 8) { bits -= 8; out[o++] = (buf >> bits) & 0xff; }')
    L.append('  }')
    L.append('  return out.subarray(0, o);')
    L.append('}')
    L.append('')

    for f in faces:
        v = f'A{f["px"]}'
        # An upscaled face borrows its BASE's glyph table and decoded bitmap — the same objects,
        # so the renderer's identity check (a scaled glyph is its base glyph) holds and no bytes
        # are duplicated. The metrics are the base's too (see Face.upscale).
        src_v = f'A{f["srcPx"]}'
        L.append(f'export const {v}: Face = {{')
        L.append(f'  firstChar: {f["first"]},')
        if f['extra']:
            L.append('  extra: new Map<number, Glyph>([')
            for (cp, off, w, h, adv, bx, by) in f['extra']:
                L.append(f'    [{cp}, {{ off: {off}, w: {w}, h: {h}, advance: {adv}, '
                         f'bx: {bx}, by: {by} }}],')
            L.append('  ]),')
        else:
            L.append('  extra: new Map(),')
        L.append(f'  px: {f["px"]},')
        L.append(f'  ascent: {f["ascent"]},')
        L.append(f'  descent: {f["descent"]},')
        L.append(f'  lineHeight: {f["lineHeight"]},')
        L.append(f'  glyphs: {src_v}_GLYPHS,')
        L.append(f'  bits: {src_v}_BITS,')
        L.append(f'  upscale: {f["upscale"]},')
        L.append(f'  upscaleFromPx: {f["upscaleOf"]},')
        L.append('};')
        L.append('')

    # The ladder, ascending, so a caller can show it and index FACES by font id. The names are
    # size-derived (FONT_20, FONT_64) and the id is the INDEX, matching the firmware's font_id_t
    # built from the same table — so a face cannot be addressed by a different number here than
    # on the device.
    L.append("/** Face ids: the INDEX into FACES, matching the firmware's font_id_t. */")
    for i, f in enumerate(faces):
        L.append(f'export const FONT_{f["px"]} = {i};')
    L.append('')
    L.append(f'export const FACES: Face[] = [{", ".join(f"A{f['px']}" for f in faces)}];')
    L.append('')
    L.append('/** The pixel sizes the panel can actually draw, ascending. */')
    L.append(f'export const FACE_PX: number[] = [{", ".join(str(f["px"]) for f in faces)}];')
    L.append('')

    # THE TWO ROLE FACES, mirroring fonts.h's FONT_BODY/FONT_VALUE #defines. Callers that want
    # "the body face" or "the hero face" name these rather than a size, so a ladder change does
    # not silently repoint them. Emitted only when the ladder actually has them — a ladder
    # without a 20 or 64 would otherwise produce a dangling reference rather than a clear error.
    for role, want in (('FONT_BODY', 20), ('FONT_VALUE', 64)):
        if any(f['px'] == want for f in faces):
            L.append(f"/** The {want}px face, by role (mirrors the firmware's {role}). */")
            L.append(f'export const {role} = FONT_{want};')
        else:
            L.append(f"// NOTE: the ladder has no {want}px face, so {role} is not defined; "
                     f"callers naming it by role must be updated.")
    L.append('')

    # The face OBJECTS by role, for callers that want the metrics rather than the id.
    for name, want in (('BODY', 20), ('VALUE', 64)):
        if any(f['px'] == want for f in faces):
            L.append(f'export const {name}: Face = A{want};')
    L.append('')

    open(out_path, 'w', encoding='utf-8').write('\n'.join(L) + '\n')
    print(f'wrote {out_path}: {len(faces)} faces, ladder '
          f'{",".join(str(f["px"]) for f in faces)}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    emit(parse_header(sys.argv[1], sys.argv[2]), sys.argv[3])
