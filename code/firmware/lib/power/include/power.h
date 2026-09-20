#pragma once

/* VBAT sense divider on the ESP32-M1 (verified from the schematic):
 *   VREF = VBAT * R9/(R8+R9) = VBAT * 1M/(300k+1M)
 * R8 = 300k from VBAT, R9 = 1M to GND, landing on GPIO26 (ADC2_CH9).
 * Therefore VBAT = VREF / VBAT_DIVIDER_RATIO. */
#define VBAT_DIVIDER_R_TOP_OHMS    300000.0
#define VBAT_DIVIDER_R_BOTTOM_OHMS 1000000.0
#define VBAT_DIVIDER_RATIO \
    (VBAT_DIVIDER_R_BOTTOM_OHMS / (VBAT_DIVIDER_R_TOP_OHMS + VBAT_DIVIDER_R_BOTTOM_OHMS))

/* Convert a measured voltage at the VREF node to actual battery volts. */
double power_vbat_from_vref(double vref_volts);

/* Linear-map a raw ADC reading to volts given the ADC full-scale reference. */
double power_vref_from_raw(int raw, int max_raw, double vref_fullscale);

typedef enum {
    POWER_SOURCE_UNKNOWN = 0,
    POWER_SOURCE_BATTERY,
    POWER_SOURCE_USB
} power_source_t;

/* Classify the supply.
 *
 * HARDWARE LIMIT (verified from the schematic): there is NO confirmed USB-present or
 * charge-status pin on this board — a VBUS net and the LTC4054's CHRG output exist, but
 * neither destination GPIO resolves — so the mode is INFERRED from VBAT alone and can be
 * wrong. The config therefore carries a user `powerMode` override
 * ('auto' | 'always-on' | 'battery') so a bad guess is correctable (FR-8:
 * "user-selectable / auto-detected"). Thresholds are Li-ion-typical and MUST be
 * calibrated on real hardware; a charging cell sits above ~4.15 V. trend_v_per_min > 0
 * means voltage is rising.
 *
 * The trend term is what disambiguates a charged pack from mains: `trend >= 0` means "not
 * falling", so a full cell that is actually DISCHARGING (high voltage, falling trend) is
 * classified BATTERY, not USB. Do not "simplify" this to a voltage-only test — that is
 * the one change that would let an unplugged full pack read as mains. */
power_source_t power_classify(double vbat_volts, double trend_v_per_min);

/* The user's powerMode override (FR-8), carried in the config document.
 *
 * WHY THIS EXISTS AT ALL: power_classify() reads VBAT alone, because this board has no
 * resolvable USB-present pin — so a full resting cell sits above the mains threshold and is
 * genuinely indistinguishable from USB *at the moment of measurement*. Inferred, therefore
 * fallible. This is the correction the user applies: 'auto' accepts the inference, and the
 * two explicit values override it.
 *
 * WHAT THE OVERRIDE CANNOT DO: 'always-on' cannot make the device sleep less than USB
 * already does, and 'battery' does not disable the USB path — it is about which power
 * BEHAVIOUR the device adopts, not about where the electrons come from. So the override
 * picks the behaviour and leaves the physical source as measured; a device explicitly set to
 * 'always-on' while actually on battery is the user's call to make, and the battery reading
 * in /api/status still reports the truth. */
typedef enum {
    POWER_MODE_AUTO = 0,      /* trust the VBAT inference */
    POWER_MODE_ALWAYS_ON,     /* behave as mains: stay awake, serve the API */
    POWER_MODE_BATTERY        /* behave as battery: deep-sleep between refreshes */
} power_mode_t;

/* Whether a mode is one of the three. Returns 1 if usable. */
int power_mode_is_valid(int mode);

/* Apply the override to a classified source. With POWER_MODE_AUTO (or an unrecognised value)
 * the classification passes through unchanged, so a config from a newer web app cannot make
 * the device behave as neither mode. */
power_source_t power_apply_mode(power_source_t detected, int mode);

/* Parse the config's `powerMode` string ("auto" | "always-on" | "battery"). Returns
 * POWER_MODE_AUTO for NULL, "", or anything unrecognised — an unknown mode must fall back to
 * the inference, never to a fixed behaviour the user did not ask for. */
int power_mode_from_string(const char *s);
