#include "provscreen.h"
#include "canvas.h"
#include "fonts.h"
#include "qr_data.h"
#include "qrcodegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Encode `payload` and blit it as a QR code filling a `box` square at (x, y).
 *
 * Used for the auto-provisioning code, whose payload embeds this device's own BLE name and so
 * cannot be pre-generated. Two ~4 KB work buffers are needed (qrcodegen's temp buffer and the
 * symbol), which is why the caller keeps this off any small stack.
 *
 * The scale is an INTEGER number of pixels per module, chosen as the largest that fits the box.
 * A fractional scale would give modules differing pixel widths and can stop a scanner
 * resolving them, so if one module does not fit the box this refuses rather than shrinking. */
int provscreen_blit_qr(canvas_t *c, int x, int y, int box, const char *payload)
{
    if (!c || !payload || !*payload) return -1;

    /* The two work buffers are HEAP-ALLOCATED, not static, and the size is why.
     *
     * qrcodegen needs 3,918 bytes for the symbol plus 3,918 for a scratch buffer. As `static`
     * arrays that is 7.8 KiB of .bss, and on this part .bss abuts the DRAM heap region the
     * framebuffers come from — so adding them pushed the largest free block down to 61,440
     * bytes and the render path could no longer allocate its 78,200-byte framebuffer at all.
     * The device booted with a stale image and an error, which is the exact failure the
     * renderer's comment warns about. Measured, not guessed: RAM usage rose by 7,864 bytes and
     * the transient-framebuffer allocation started failing immediately.
     *
     * Taken and given back around the one encode, so the cost is a brief 7.8 KiB that is
     * released before provisioning needs its memory. */
    uint8_t *qr = malloc(qrcodegen_BUFFER_LEN_MAX);
    uint8_t *tmp = malloc(qrcodegen_BUFFER_LEN_MAX);
    if (!qr || !tmp) {
        free(qr); free(tmp);
        return -1;
    }

    /* ECC LOW, and boostEcl off: the payload is ~60 bytes and must stay at a small version so
     * the modules are large enough to scan at arm's length. Boosting the ECC level would push
     * the version up and the module size down for protection this screen does not need — it is
     * displayed on clean glass at close range, not printed and folded. */
    const int ok = qrcodegen_encodeText(payload, tmp, qr, qrcodegen_Ecc_LOW,
                                        1, qrcodegen_VERSION_MAX,
                                        qrcodegen_Mask_AUTO, false);
    free(tmp);   /* not needed once the symbol is built */
    if (!ok) {
        free(qr);
        return -1;
    }

    const int size = qrcodegen_getSize(qr);
    const int k = box / size;
    if (k < 1) {
        free(qr);
        return -1;
    }

    const int total = k * size;
    const int ox = x + (box - total) / 2;
    const int oy = y + (box - total) / 2;

    /* Clear a generous surround. The QR standard's 4-module quiet zone is included by drawing
     * the light modules of the symbol itself as blank, but the page around it must also be
     * clear or the code has no contrast against whatever it abuts. */
    for (int dy = -10; dy < total + 10; dy++)
        for (int dx = -10; dx < total + 10; dx++)
            canvas_set_px(c, ox + dx, oy + dy, 0);

    for (int my = 0; my < size; my++) {
        for (int mx = 0; mx < size; mx++) {
            if (!qrcodegen_getModule(qr, mx, my)) continue;   /* true = dark */
            for (int dy = 0; dy < k; dy++)
                for (int dx = 0; dx < k; dx++)
                    canvas_set_px(c, ox + mx * k + dx, oy + my * k + dy, 1);
        }
    }
    free(qr);
    return 0;
}

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

/* Column geometry.
 *
 * THE QR BOX IS DERIVED FROM THE PANEL, NOT CHOSEN. A first attempt hardcoded 190 px and the
 * right-hand column landed at x=920 — one pixel past the 920-wide panel — so a third of two
 * codes was off the glass. Nothing errored: the modules were simply never drawn, which the
 * round-trip test caught by finding mismatches at x=920. Deriving the width from what is left
 * after the text column means the layout cannot overflow however the pieces are resized.
 *
 * LEFT_W is set so the widest string at the scale it is drawn at fits: the AP name at 2x
 * measures 482 px (font_measure). An overflowing line does not wrap — it runs into the QR
 * column and eats the codes, which a screenshot showed happening at 430. */
#define MARGIN      30
#define LEFT_W      486
#define QR_GAP      18
#define QR_BOX_W    ((EPD_WIDTH - 2 * MARGIN - LEFT_W - QR_GAP - QR_COL_GAP) / 2)
#define QR_COL_GAP  18           /* between the two columns of codes */
#define QR_ROW_GAP  16           /* between the two rows of codes */

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
    return provscreen_render_branded(fb, ap_ssid, pop, NULL, 0, 0);
}

int provscreen_render_branded(uint8_t *fb, const char *ap_ssid, const char *pop,
                              const uint8_t *badge, int badge_w, int badge_h)
{
    if (!fb || !ap_ssid || !*ap_ssid) return -1;

    canvas_t c;
    int all_ok = 1;
    canvas_init(&c, fb);
    canvas_fill(&c, 0);              /* white page */

    /* ---- Header: the brand, then the title ----
     * The badge is drawn by the CALLER through provscreen_draw_badge() before this runs, because
     * this library does not know about the firmware's generated assets. The title is offset to
     * leave room for it; BADGE_W matches the generated badge's width. */
    int y = MARGIN + 4;
    const int line2 = font_line_height(FONT_BODY) * 2;   /* the title's height at 2x */
    if (badge && badge_w > 0 && badge_h > 0) {
        /* Vertically centred against the title. canvas_blit_1bpp takes the SAME polarity the
         * asset is packed in (bit clear = black), so this is a straight copy. */
        const int by = y + (line2 - badge_h) / 2;
        canvas_blit_1bpp(&c, MARGIN, by, badge, badge_w, badge_h);
    }
    const int title_x = MARGIN + (badge ? badge_w + 18 : 0);
    draw_line_scaled(&c, FONT_BODY, title_x, y, "SET UP DISPLAY", 2);
    y += (badge_h > line2 ? badge_h : line2) + 14;

    /* A rule under the header, drawn as a filled row of pixels. */
    for (int x = MARGIN; x < MARGIN + LEFT_W; x++) canvas_set_px(&c, x, y, 1);
    y += 20;

    /* ---- Fastest path: the setup page ---- */
    y += draw_line(&c, FONT_BODY, MARGIN, y, "EASIEST WAY - no app needed") + 12;
    y += draw_line(&c, FONT_BODY, MARGIN, y,
                   "1. Join this WiFi network on your phone.") + 10;
    draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, ap_ssid, 2);
    y += font_line_height(FONT_BODY) * 2 + 14;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "2. Scan code 2 and enter your") + 0;
    y += draw_line(&c, FONT_BODY, MARGIN, y,
                   "WiFi, weather API key and location.") + 22;

    /* ---- The app path ---- */
    y += draw_line(&c, FONT_BODY, MARGIN, y, "OR - ESP BLE PROVISIONING APP") + 10;
    y += draw_line(&c, FONT_BODY, MARGIN, y,
                   "Install it with code 3 or 4, then scan") + 0;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "code 1 - no typing.") + 14;
    y += draw_line(&c, FONT_BODY, MARGIN, y, "Bluetooth device:") + 2;
    draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, ap_ssid, 1);
    y += font_line_height(FONT_BODY) + 12;
    if (pop && *pop) {
        y += draw_line(&c, FONT_BODY, MARGIN, y, "Proof of possession:") + 2;
        draw_line_scaled(&c, FONT_BODY, MARGIN + 22, y, pop, 2);
        y += font_line_height(FONT_BODY) * 2 + 6;
    }

    /* ---- QR grid: 2 x 2 ----
     * The auto-provisioning code is FIRST because it is the convenient path: scanning it in the
     * ESP BLE Provisioning app carries the device name, the PoP and the transport all at once,
     * so the user never types a device name or a key. */
    const int qx = MARGIN + LEFT_W + QR_GAP;
    const int qy0 = MARGIN + 8;

    /* The payload the app expects, per Espressif's documented format:
     *   {"ver":"v1","name":"<BLE name>","pop":"<PoP>","transport":"ble"}
     * "name" is what the app matches against the advertiser, so it must be the same string the
     * device advertises — which is why it is built from the same ap_ssid passed in. */
    char prov_payload[160];
    if (pop && *pop) {
        snprintf(prov_payload, sizeof(prov_payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
                 ap_ssid, pop);
    } else {
        snprintf(prov_payload, sizeof(prov_payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"transport\":\"ble\"}", ap_ssid);
    }

    struct { const char *cap; const qr_code_t *qr; const char *payload; } codes[] = {
        { "1. BLE SETUP",            NULL,        prov_payload },
        { "2. SETUP PAGE",           &QR_PORTAL,  NULL },
        { "3. iOS APP",              &QR_IOS,     NULL },
        { "4. ANDROID APP",          &QR_ANDROID, NULL },
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        const int col = (int)(i % 2);
        const int row = (int)(i / 2);
        const int cx = qx + col * (QR_BOX_W + QR_COL_GAP);
        const int cy = qy0 + row * (QR_BOX_W + font_line_height(FONT_BODY) + QR_ROW_GAP);

        int ok = 1;
        if (codes[i].qr) {
            draw_qr(&c, cx, cy, QR_BOX_W, codes[i].qr);
        } else {
            /* The runtime-encoded code. A failure here (payload too long, or a box too small
             * for one module per pixel) leaves the rest of the screen intact and is reported
             * through the return value rather than a log call: this library is host-tested and
             * so cannot depend on esp_log. */
            ok = provscreen_blit_qr(&c, cx, cy, QR_BOX_W, codes[i].payload) == 0;
        }
        /* Caption centred under the code, and wrapped onto a second line when it will not fit
         * the box — a caption that runs into its neighbour is worse than a smaller one. */
        const int cw = text_width(FONT_BODY, codes[i].cap);
        if (cw <= QR_BOX_W + QR_COL_GAP) {
            draw_line(&c, FONT_BODY, cx + (QR_BOX_W - cw) / 2, cy + QR_BOX_W + 2, codes[i].cap);
        } else {
            draw_line_scaled(&c, FONT_BODY, cx, cy + QR_BOX_W + 2, codes[i].cap, 1);
        }
        if (!ok) all_ok = 0;
    }
    return all_ok ? 0 : -1;
}
