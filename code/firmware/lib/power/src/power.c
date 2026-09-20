#include "power.h"
#include <string.h>
#include <math.h>

double power_vbat_from_vref(double vref_volts)
{
    if (!isfinite(vref_volts)) return 0.0;
    return vref_volts / VBAT_DIVIDER_RATIO;
}

double power_vref_from_raw(int raw, int max_raw, double vref_fullscale)
{
    if (max_raw <= 0) return 0.0;
    return ((double)raw / (double)max_raw) * vref_fullscale;
}

power_source_t power_classify(double vbat_volts, double trend_v_per_min)
{
    if (!isfinite(vbat_volts) || vbat_volts <= 0.0) return POWER_SOURCE_UNKNOWN;
    if (vbat_volts >= 4.15 && trend_v_per_min >= 0.0) return POWER_SOURCE_USB;
    return POWER_SOURCE_BATTERY;
}

/* ---- the powerMode override (FR-8) ---- */

int power_mode_is_valid(int mode)
{
    return mode == POWER_MODE_AUTO || mode == POWER_MODE_ALWAYS_ON || mode == POWER_MODE_BATTERY;
}

int power_mode_from_string(const char *s)
{
    if (!s || !*s) return POWER_MODE_AUTO;
    if (strcmp(s, "auto") == 0) return POWER_MODE_AUTO;
    if (strcmp(s, "always-on") == 0) return POWER_MODE_ALWAYS_ON;
    if (strcmp(s, "battery") == 0) return POWER_MODE_BATTERY;

    /* An unrecognised mode falls back to the INFERENCE, not to a fixed behaviour: the user
     * did not ask for mains or for battery, so adopting either would be inventing a choice. */
    return POWER_MODE_AUTO;
}

power_source_t power_apply_mode(power_source_t detected, int mode)
{
    switch (mode) {
    case POWER_MODE_ALWAYS_ON: return POWER_SOURCE_USB;
    case POWER_MODE_BATTERY:   return POWER_SOURCE_BATTERY;
    default:                   return detected;   /* auto, or anything invalid */
    }
}
