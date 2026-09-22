/**
 * The weather icon mapping, mirroring the firmware's lib/layout/src/weather_icons.c.
 *
 * WHY THIS MUST MATCH EXACTLY: the config app previews the panel, and the two run in different
 * languages. If this mapping disagreed with the firmware's, the preview would show a sun while
 * the glass showed rain — and it would look like a device fault. The golden-image test covers
 * the default layout's fields; this covers the icon codes, which the layout need not contain.
 *
 * The mapping is by the code's VALUE, not its leading digit: OWM's codes are 01..04, 09, 10, 11,
 * 13 and 50, so "01" (clear) and "09" (shower) share a leading 0 but are different weather.
 */

export const enum WeatherIcon {
  CLEAR = 0,
  CLEAR_NIGHT = 1,
  PARTLY = 2,
  PARTLY_NIGHT = 3,
  CLOUDY = 4,
  RAIN = 5,
  STORM = 6,
  SNOW = 7,
  FOG = 8,
  UNKNOWN = -1,
}

/**
 * Map an OpenWeatherMap icon code ("04n") to an icon index, or UNKNOWN.
 *
 * The day/night letter is used only for the two conditions that look different after dark —
 * clear and partly-cloudy — which keeps the set to nine icons without showing a sun at 2 a.m.
 * Anything unparseable or outside the known groups is UNKNOWN, which the renderer draws as
 * nothing rather than as a guessed icon.
 */
export function weatherIconIndex(code: string | undefined): WeatherIcon {
  if (!code || code.length < 2) return WeatherIcon.UNKNOWN;

  const digits = code.slice(0, 2);
  if (!/^[0-9]{2}$/.test(digits)) return WeatherIcon.UNKNOWN;
  const n = Number(digits);

  /* A missing suffix is treated as day — see the firmware's comment for why day is the
   * conservative default for a malformed code. */
  const night = code[2] === 'n';

  switch (n) {
    case 1: return night ? WeatherIcon.CLEAR_NIGHT : WeatherIcon.CLEAR;
    case 2: return night ? WeatherIcon.PARTLY_NIGHT : WeatherIcon.PARTLY;
    case 3:
    case 4: return WeatherIcon.CLOUDY;
    case 9:
    case 10: return WeatherIcon.RAIN;
    case 11: return WeatherIcon.STORM;
    case 13: return WeatherIcon.SNOW;
    case 50: return WeatherIcon.FOG;
    default: return WeatherIcon.UNKNOWN;
  }
}
