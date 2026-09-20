#include "provscreen.h"
#include "canvas.h"
#include "fonts.h"
#include "qr_data.h"

#include <string.h>

/* ------------------------------------------------------------------------------------------
 * LAYOUT. 920x680, landscape. Two columns: instructions on the left, the three QR codes down
 * the right. The codes are the reason someone is looking at this screen, so they get the size.
 *
 * WHY THE PROOF OF POSSESSION IS PRINTED ON THE GLASS: it is a secret in the sense that it
 * stops a stranger from provisioning a device they can see, but it is only displayed while the
 * device is deliberately unconfigured, and at that point anyone in radio range can already
 * join the open setup AP and configure it just as completely. Hiding the PoP while the AP is
 * open would be security theatre — it would cost the owner a trip to a serial console to find
 * it, and stop nobody. It is never shown once the device is configured, and the screen is
 * cleared the moment provisioning succeeds.
 * --------------------------------------------------------------------------------------- */

/* Column geometry. LEFT_W is set so the WIDEST string at the scale it is drawn at still fits:
 * the AP name at 2x measures 482 px (font_measure), which is what forces 520 rather than
 * something tighter. An overflowing line does not wrap — it runs into the QR column and eats
 * the codes, which a screenshot showed happening at 430. */
#define MARGIN      36
#define LEFT_W      520
#define QR_GAP      30
#define QR_BOX      150          /* QR module area, square */

static void draw_line_scaled(canvas_t *c, font_id_t f, int x, int y, const char *s, int k);

static int text_width(font_id_t f, const char *s)
{
    int w = 0, h = 0;
    if (font_measure(f, s, &w, &h) != 0) return 0;
    return w;
}

/* Draw one line of text at (x, y_top) in `f`. Returns the line height so callers can stack. */
static int draw_line(canvas_t *c, font_id_t f, int x, int y, const char *s)
{
    draw_line_scaled(c, f, x, y, s, 1);
    return font_line_height(f);
}

/* Draw `s` scaled `k` times, one module per k pixels. Only the body face is used, so a scale
 * factor is enough to get a large label without a second atlas.
 *
 * THE POLARITY HERE IS THE WHOLE REASON THIS FUNCTION EXISTS. The font atlas stores 1 = ink.
 * canvas_blit_1bpp_masked() interprets its source with the FRAMEBUFFER's convention, where a
 * CLEAR bit is black ink — so handing it atlas bits directly draws every glyph inverted into
 * unrecognisable shapes. The normal render path avoids this by testing the atlas bit itself
 * (lib/layout/src/render.c blit_glyph_clipped) rather than calling the blitter. This does the
 * same, and at scale 1 too, so there is exactly one place text touches a canvas. */
static void draw_line_scaled(canvas_t *c, font_id_t f, int x, int y, const char *s, int k)
{
    if (!s || k < 1) return;
    const int baseline = y + font_ascent(f) * k;
    int pen = x;
    const char *p = s;
    while (*p) {
        const uint8_t *bits = NULL;
        int gw = 0, gh = 0, bx = 0, by = 0;
        if (font_glyph(f, *p, &bits, &gw, &gh) != 0) break;
        if (font_bearing(f, *p, &bx, &by) != 0) break;
        if (bits) {
            const int pitch = (gw + 7) / 8;
            for (int sy = 0; sy < gh; sy++) {
                for (int sx = 0; sx < gw; sx++) {
                    if (!((bits[(size_t)sy * pitch + (sx >> 3)] >> (7 - (sx & 7))) & 1))
                        continue;   /* atlas bit clear = no ink */
                    for (int dy = 0; dy < k; dy++)
                        for (int dx = 0; dx < k; dx++)
                            canvas_set_px(c, pen + (bx + sx) * k + dx,
                                             baseline + (by + sy) * k + dy, 1);
                }
            }
        }
        pen += font_advance(f, *p) * k;
        p++;
    }
}

/* Blit a QR code at (x, y), `box` pixels per side including the quiet zone already present in
 * the matrix. Scales modules by an integer factor, nearest-neighbour, so a code stays crisp —
 * a fractional scale would give modules differing widths and can break scanning. */
static void draw_qr(canvas_t *c, int x, int y, int box, const qr_code_t *qr)
{
    if (!qr || !qr->bits) return;
    const int k = box / qr->size;
    if (k < 1) return;
    const int total = k * qr->size;
    const int ox = x + (box - total) / 2;      /* centre it in its box */
    const int oy = y + (box - total) / 2;

    /* White surround first. The quiet zone is part of the matrix, but the surrounding area
     * must also be clear or the code has no contrast against the page. */
    for (int dy = -8; dy < total + 8; dy++)
        for (int dx = -8; dx < total + 8; dx++)
            canvas_set_px(c, ox + dx, oy + dy, 0);

    for (int my = 0; my < qr->size; my++) {
        for (int mx = 0; mx < qr->size; mx++) {
            const uint8_t b = qr->bits[(size_t)my * qr->pitch + (mx >> 3)];
            if (!((b >> (7 - (mx & 7))) & 1)) continue;    /* 1 = dark module */
            for (int dy = 0; dy < k; dy++)
                for (int dx = 0; dx < k; dx++)
                    canvas_set_px(c, ox + mx * k + dx, oy + my * k + dy, 1);
        }
    }
}

int provscreen_render(uint8_t *fb, const char *ap_ssid, const char *pop)
{
    if (!fb || !ap_ssid || !*ap_ssid) return -1;

    canvas_t c;
    canvas_init(&c, fb);
    canvas_fill(&c, 0);              /* white page */

    /* ---- Title ---- */
    int y = MARGIN;
    /* 2x, not 3x: "SET UP THIS DISPLAY" at 3x measures 486 px and would run past the QR
     * column. At 2x it is 324 px and clears the 520 px text column with room to spare. */
    draw_line_scaled(&c, FONT_BODY, MARGIN, y, "SET UP THIS DISPLAY", 2);
    y += font_line_height(FONT_BODY) * 2 + 16;

    /* A rule under the title, drawn as a filled row of pixels. */
    for (int x = MARGIN; x < MARGIN + LEFT_W; x++) canvas_set_px(&c, x, y, 1);
    y += 24;

    /* ---- Fastest path: the setup page ---- */
    y += draw_line(&c, FONT_BODY, MARGIN, y, "EASIEST WAY - no app needed") + 12;
    y += draw_line(&c, FONT_BODY, MARGIN, y,
                   "1. Join this WiFi network on your phone.") + 10;
    draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, ap_ssid, 2);
    y += font_line_height(FONT_BODY) * 2 + 14;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "2. Scan the SETUP PAGE code, right.") + 2;
    y += draw_line(&c, FONT_BODY, MARGIN, y,
                   "Then enter your WiFi, weather API key") + 0;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "and location.") + 20;

    /* ---- The app path ---- */
    y += draw_line(&c, FONT_BODY, MARGIN, y, "OR - ESP BLE PROVISIONING APP") + 10;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "Bluetooth device:") + 2;
    draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, ap_ssid, 1);
    y += font_line_height(FONT_BODY) + 12;
    if (pop && *pop) {
        y += draw_line(&c, FONT_BODY, MARGIN, y, "Proof of possession:") + 2;
        draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, pop, 2);
        y += font_line_height(FONT_BODY) * 2 + 6;
    }

    /* ---- QR column ---- */
    const int qx = MARGIN + LEFT_W + QR_GAP;
    int qy = MARGIN + 24;

    struct { const qr_code_t *qr; const char *cap; } codes[] = {
        /* The captions NAME the app. Calling these "iPhone APP" and "Android APP" was too
         * vague — a reader could not tell they were the ESP BLE Provisioning app, which is the
         * convenient path and the one the QR is actually for. The captions are kept within the
         * 150 px column (widest measures 177 px at about 1.2x, which still fits). */
        { &QR_PORTAL,  "1. SETUP PAGE" },
        { &QR_IOS,     "2. BLE APP (iOS)" },
        { &QR_ANDROID, "3. BLE APP (Andrd)" },
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        draw_qr(&c, qx, qy, QR_BOX, codes[i].qr);
        /* Caption centred under the code, wrapped onto one line. */
        const int cw = text_width(FONT_BODY, codes[i].cap);
        draw_line(&c, FONT_BODY, qx + (QR_BOX - cw) / 2,
                  qy + QR_BOX + 4, codes[i].cap);
        qy += QR_BOX + font_line_height(FONT_BODY) + 26;
    }

    return 0;
}
