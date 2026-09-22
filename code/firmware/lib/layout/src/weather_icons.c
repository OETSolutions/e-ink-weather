#include "weather_icons.h"

/* See weather_icons.h for the contract. Kept separate from the generated data so the mapping is
 * readable and testable without the 4.6 KB of bitmap in the same file. */

int weather_icon_index(const char *code)
{
    if (!code) return WEATHER_ICON_UNKNOWN;

    /* Parse the two leading digits. A code that does not start with a digit, or has only one,
     * is not an OWM icon code and must not be guessed at. */
    int n = 0;
    int digits = 0;
    for (const char *p = code; digits < 2; p++, digits++) {
        if (*p < '0' || *p > '9') return WEATHER_ICON_UNKNOWN;
        n = n * 10 + (*p - '0');
    }

    /* The third character is the day/night suffix. Absent is treated as day: a code like "01"
     * without the letter is malformed, and day is the more conservative default — the sun is the
     * icon a user would recognise for a clear sky, and showing it is less jarring than assuming
     * night on a code that meant to say nothing about the time. */
    const char suffix = code[2];
    const int night = (suffix == 'n');

    /* Group by the code's own value.
     *
     * SWITCHING ON THE NUMBER, not on its first digit: OWM's codes are 01..04, 09, 10, 11, 13
     * and 50, so the leading digit alone does not delimit the groups — "09" (shower) and "01"
     * (clear) share a leading 0 but are different weather, and an earlier version that grouped
     * on n/10 mapped every 0x code to clear. The second digit only distinguishes intensity
     * within a group (09 shower vs 10 rain), which this set deliberately does not render
     * differently. */
    switch (n) {
        case 1:                 /* 01x: clear sky */
            return night ? WEATHER_ICON_CLEAR_NIGHT : WEATHER_ICON_CLEAR;
        case 2:                 /* 02x: few clouds — the only cloudy code that keeps the sun
                                 * visible, so only it gets the day/night split. */
            return night ? WEATHER_ICON_PARTLY_NIGHT : WEATHER_ICON_PARTLY;
        case 3:                 /* 03x: scattered clouds, 04x: broken — no sun either way */
        case 4:
            return WEATHER_ICON_CLOUDY;
        case 9:                 /* 09x: shower, 10x: rain */
        case 10:
            return WEATHER_ICON_RAIN;
        case 11:                /* 11x: thunderstorm */
            return WEATHER_ICON_STORM;
        case 13:                /* 13x: snow */
            return WEATHER_ICON_SNOW;
        case 50:                /* 50x: mist, fog, haze, smoke, dust — all low visibility */
            return WEATHER_ICON_FOG;
        default:
            return WEATHER_ICON_UNKNOWN;
    }
}
