/* Generates tools/golden/default_layout.bin — the byte-for-byte regression lock for the
 * composite renderer (NFR-9), plus default_layout.json so the web app's renderer can be
 * checked against the same spec (Task 17).
 *
 * WHY A SEPARATE HOST PROGRAM: the golden must be produced by the same renderer the device
 * runs, or it locks nothing. Building it here against lib/layout directly means any change
 * to render.c, fonts.c or the atlas changes the golden — which is exactly the signal wanted.
 *
 * The layout spec lives in golden/default_layout.h, which the golden TEST also includes, so
 * the two cannot drift apart.
 *
 * Regenerate ONLY with a deliberate, reviewed reason, and LOOK at the output:
 *   cc -I lib/layout/include -I lib/layout/src -o /tmp/gen_golden tools/gen_golden.c \
 *      lib/layout/src/canvas.c lib/layout/src/fonts.c lib/layout/src/render.c \
 *      lib/layout/src/weather_icons.c lib/layout/src/weather_icons_data.c
 *   /tmp/gen_golden tools/golden/default_layout.bin
 *   python3 tools/dump_1bpp.py tools/golden/default_layout.bin /tmp/golden.png
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canvas.h"
#include "render.h"
#include "default_layout.h"

static int write_json(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "{\n  \"schemaVersion\": 1,\n  \"updateSeconds\": 900,\n"
               "  \"partialRefreshLimit\": 24,\n  \"pages\": [\n"
               "    { \"name\": \"default\", \"refreshSeconds\": 900, \"weight\": 1 }\n"
               "  ],\n");
    /* The STATIC LAYER is emitted too, not just the fields.
     *
     * WHY: the web app's cross-check (Task 17) has to reproduce this exact image, and the
     * static layer is most of it — the labels and rules are what the golden is mostly made
     * of. A fixture with only the fields would force the web test to hardcode the labels
     * from this header, which is the drift the single-source-of-truth note above exists to
     * prevent: change a label here, regenerate, and the test would keep checking the old
     * one against a golden built from the new one. Emitting them keeps the fixture complete. */
    fprintf(f, "  \"labels\": [\n");
    for (int i = 0; i < GOLDEN_LABEL_COUNT; i++) {
        const static_label_t *l = &GOLDEN_LABELS[i];
        fprintf(f, "    { \"x\": %d, \"y\": %d, \"text\": \"%s\", \"font\": %d }%s\n",
                l->x, l->y, l->text, l->font_id,
                i + 1 < GOLDEN_LABEL_COUNT ? "," : "");
    }
    fprintf(f, "  ],\n  \"rules\": [\n");
    for (int i = 0; i < GOLDEN_RULE_COUNT; i++) {
        fprintf(f, "    { \"y\": %d, \"thickness\": %d, \"inset\": %d }%s\n",
                GOLDEN_RULES[i].y, GOLDEN_RULES[i].thickness, GOLDEN_RULES[i].inset,
                i + 1 < GOLDEN_RULE_COUNT ? "," : "");
    }
    fprintf(f, "  ],\n  \"fields\": [\n");
    for (int i = 0; i < GOLDEN_FIELD_COUNT; i++) {
        const value_field_t *v = &GOLDEN_FIELDS[i];
        /* `kind` travels with the box (see value_field_t): 'i' boxes draw a weather icon from
         * their sample code rather than setting it as text. The web app's cross-check needs it
         * or it would render an icon box as the literal string "04d". */
        fprintf(f, "    { \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, "
                   "\"alignH\": \"%c\", \"alignV\": \"%c\", \"font\": %d, "
                   "\"kind\": \"%c\", \"sample\": \"%s\" }%s\n",
                v->x, v->y, v->w, v->h, v->align_h, v->align_v, v->font_id,
                (v->kind == VALUE_KIND_ICON) ? 'i' : 't',
                GOLDEN_VALUES[i], i + 1 < GOLDEN_FIELD_COUNT ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "tools/golden/default_layout.bin";

    uint8_t *static_layer = malloc(EPD_FB_BYTES);
    uint8_t *fb = malloc(EPD_FB_BYTES);
    if (!static_layer || !fb) { fprintf(stderr, "oom\n"); return 1; }

    golden_build_static_layer(static_layer);

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, GOLDEN_FIELDS, GOLDEN_VALUES, GOLDEN_FIELD_COUNT);

    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", out); return 1; }
    size_t n = fwrite(fb, 1, EPD_FB_BYTES, f);
    fclose(f);
    if (n != EPD_FB_BYTES) { fprintf(stderr, "short write\n"); return 1; }
    printf("wrote %s (%zu bytes)\n", out, n);

    if (write_json("tools/golden/default_layout.json") == 0) {
        printf("wrote tools/golden/default_layout.json\n");
    }

    free(static_layer);
    free(fb);
    return 0;
}
