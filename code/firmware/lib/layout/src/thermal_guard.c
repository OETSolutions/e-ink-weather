#include "thermal_guard.h"

thermal_state_t thermal_check(int panel_temp_c, int temp_valid)
{
    if (!temp_valid) return THERMAL_UNKNOWN;      /* never guess from a missing sensor */
    if (panel_temp_c <= PANEL_TEMP_SENTINEL_C) return THERMAL_UNKNOWN;  /* failed read */
    if (panel_temp_c < PANEL_TEMP_MIN_C)       return THERMAL_TOO_COLD;
    if (panel_temp_c > PANEL_TEMP_MAX_C)       return THERMAL_TOO_HOT;
    return THERMAL_OK;
}
