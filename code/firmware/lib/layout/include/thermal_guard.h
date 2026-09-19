#pragma once

/* The GDEH0576T81's documented operating range is TOPR 0..50 degC (panel datasheet §3).
 * Below 0 the EPD's behaviour is unspecified, so rendering is not attempted.
 *
 * TEMPERATURE PROVENANCE (verified on hardware 2026-09-18):
 * This build has NO trustworthy ambient temperature source, and the guard must not
 * pretend otherwise. The SSD2677's internal sensor does not return live data on this
 * unit: 0x40 TSC reads a constant -15 degC, 0x43 TSR a constant -1 degC, and the
 * documented TSE offset register (0x41) moves NEITHER across all 8 codes. That is not a
 * transport bug — the same read path returns the datasheet-correct 0x07 from REV (0x70)
 * and 0x00 from an unused register. The panel's I2C external-sensor lines (TSCL/TSDA,
 * pins 6/7) reach GPIO22/21, but GPIO21 doubles as the microSD CS and no sensor is
 * fitted, so there is no fallback.
 *
 * Consequence, and the reason this enum has a fourth member: an absent or unusable
 * sensor is NOT evidence of cold. Collapsing "unreadable" into "too cold" made the guard
 * block every render forever on hardware that reports a constant — observed on this
 * bench. THERMAL_UNKNOWN means "cannot decide", and the caller renders and logs. */
#define PANEL_TEMP_MIN_C 0
#define PANEL_TEMP_MAX_C 50

/* The SSD2677's temperature register cannot legitimately report below -40 degC; values
 * at or under this are treated as a failed read, not a real reading. */
#define PANEL_TEMP_SENTINEL_C (-128)

typedef enum {
    THERMAL_OK = 0,
    THERMAL_TOO_COLD,
    THERMAL_TOO_HOT,
    THERMAL_UNKNOWN     /* no usable source — guard cannot decide; do not block on this */
} thermal_state_t;

/* `temp_valid` is 0 whenever the reading failed or came from an unusable source. Callers
 * MUST render on THERMAL_UNKNOWN (logging it) and MUST NOT render on TOO_COLD/TOO_HOT. */
thermal_state_t thermal_check(int panel_temp_c, int temp_valid);
