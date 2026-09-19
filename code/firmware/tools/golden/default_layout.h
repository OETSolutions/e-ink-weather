#pragma once

/* The pinned "default layout" used by BOTH the golden generator and the golden test.
 *
 * SINGLE SOURCE OF TRUTH: if the field boxes or values lived in two places they would drift,
 * and the golden would then be locking a layout nobody actually renders — the worst kind of
 * passing test. The web app's cross-check (Task 17) reads the JSON that gen_golden.c emits
 * from this same table.
 *
 * Static-layer contents are also defined here because the golden test must compose onto the
 * exact static layer the web app is expected to produce (FR-1). */

#include "canvas.h"
#include "render.h"
#include "fonts.h"

/* Labels and chrome the web app bakes into the static layer. */
typedef struct {
    int x, y;
    const char *text;
    int font_id;
} static_label_t;

#define GOLDEN_LABEL_COUNT 3
static const static_label_t GOLDEN_LABELS[GOLDEN_LABEL_COUNT] = {
    {  48,  40, "OUTDOOR",         FONT_BODY },
    {  48, 220, "INDOOR",          FONT_BODY },
    {  48, 400, "UPDATED 14:05",   FONT_BODY },
};

/* Dividers: y, thickness. Full width inset by 48px. */
typedef struct { int y, thickness; } static_rule_t;

#define GOLDEN_RULE_COUNT 2
static const static_rule_t GOLDEN_RULES[GOLDEN_RULE_COUNT] = {
    { 190, 2 },
    { 370, 2 },
};

#define GOLDEN_INSET 48

/* The dynamic value boxes the firmware stamps. */
#define GOLDEN_FIELD_COUNT 2
static const value_field_t GOLDEN_FIELDS[GOLDEN_FIELD_COUNT] = {
    { .x = 48, .y = 76,  .w = 420, .h = 110, .align_h = 'L', .align_v = 'T',
      .font_id = FONT_VALUE },
    { .x = 48, .y = 256, .w = 420, .h = 110, .align_h = 'L', .align_v = 'T',
      .font_id = FONT_VALUE },
};

static const char *const GOLDEN_VALUES[GOLDEN_FIELD_COUNT] = { "68.4", "41.2" };

/* Build the static layer the web app is expected to push: white, with labels and rules. */
static inline void golden_build_static_layer(uint8_t *fb)
{
    canvas_t c;
    canvas_init(&c, fb);
    canvas_fill(&c, 0);          /* white */

    for (int i = 0; i < GOLDEN_LABEL_COUNT; i++) {
        const static_label_t *l = &GOLDEN_LABELS[i];
        /* Labels are drawn with the renderer itself, onto the layer being built — the same
         * "compose onto the current image" call the device makes. */
        value_field_t f = { .x = l->x, .y = l->y, .w = 500, .h = 40,
                            .align_h = 'L', .align_v = 'T', .font_id = l->font_id };
        const char *v[1] = { l->text };
        render_compose(&c, fb, &f, v, 1);
    }

    for (int i = 0; i < GOLDEN_RULE_COUNT; i++) {
        for (int t = 0; t < GOLDEN_RULES[i].thickness; t++) {
            for (int x = GOLDEN_INSET; x < EPD_WIDTH - GOLDEN_INSET; x++) {
                canvas_set_px(&c, x, GOLDEN_RULES[i].y + t, 1);
            }
        }
    }
}
