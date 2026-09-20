/**
 * The shipped default layout (FR-17): a landscape 920x680 weather dashboard.
 *
 * THIS IS THE STARTING POINT A NEW DEVICE SHOWS, so it has to be a real design and not a pile
 * of boxes. The stated use case is: current conditions at a glance, the two Home Assistant
 * temperatures the user actually cares about, and a short forecast — readable across a room,
 * which sets the sizes more than anything else. The panel is 198 dpi at 920x680, so 64 px
 * numerals are roughly 8 mm tall on the glass: legible at a few metres.
 *
 * GEOMETRY RULES IT RESPECTS, and why each is a rule:
 *   - nothing outside the panel: an off-panel field is silently clipped by the renderer;
 *   - nothing overlapping: two widgets sharing pixels means one value stamps over the other,
 *     and which one wins depends on array order — the kind of bug that looks like a rendering
 *     fault. A test enforces this rather than trusting the arithmetic;
 *   - the alert bar spans the full width at the bottom so it cannot be confused with a reading.
 *
 * The layout lives here rather than in the device firmware because the device is deliberately
 * layout-INDEPENDENT (FR-1): it stamps values into boxes the web app defines, so a layout change
 * ships from the app with no firmware update.
 */

import { type Config, type Widget, SCHEMA_VERSION } from '../model/config';

/** The two Home Assistant entities the user named. */
export const HA_OUTDOOR = 'sensor.64b708cfe0fc_sensor_2_temperature_f';
export const HA_HALLWAY = 'sensor.upstairs_hallway_temperature';

/* THE TWO SIZES THE PANEL ACTUALLY HAS. Declared explicitly rather than as a free-form number:
 * the device rasterises glyphs at exactly two pixel sizes (FR-4a — a 1 bpp panel cannot
 * anti-alias, and a runtime rasteriser would cost RAM it does not have), so a widget's size
 * SELECTS a face. Writing 48 for "medium" would be a number that means nothing on the glass. */
const VALUE_PX = 64;   /* the hero readings */
const BODY_PX = 20;    /* labels, conditions, units */

/** A value box, with the defaults a weather reading wants. */
function value(
  id: string,
  x: number,
  y: number,
  w: number,
  h: number,
  over: Partial<Widget> = {},
): Widget {
  return {
    id, x, y, w, h,
    role: 'dynamic',
    format: { decimals: 1, suffix: '°F', fallback: '--' },
    font: { size: VALUE_PX, align: 'left', valign: 'top' },
    ...over,
  };
}

/** Temperature extremes worth waking someone for: a heat and a hard-freeze rule on every
 *  temperature reading, plus wind on the wind box. Advisory for a gusty day, severe for the
 *  two that damage pipes and people. */
const TEMP_ALERTS = [
  { op: 'gte' as const, threshold: 100, level: 'severe' as const },
  { op: 'lte' as const, threshold: 20, level: 'severe' as const },
  { op: 'gte' as const, threshold: 95, level: 'advisory' as const },
  { op: 'lte' as const, threshold: 32, level: 'advisory' as const },
];

export function defaultLayout(): Config {
  const page: Widget[] = [
    /* Hero: current conditions, top-left, where the eye lands first. */
    value('owm_temp', 40, 64, 360, 120, {
      binding: { kind: 'owm-current', owmField: 'temp' },
      font: { size: VALUE_PX, align: 'left', valign: 'top' },
      alerts: TEMP_ALERTS,
    }),
    value('owm_condition', 40, 196, 360, 50, {
      binding: { kind: 'owm-current', owmField: 'condition' },
      /* Words, not numbers: 48 px body face and no unit. */
      format: { fallback: '—' },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
    }),
    value('owm_wind', 40, 258, 200, 50, {
      binding: { kind: 'owm-current', owmField: 'wind' },
      format: { decimals: 0, suffix: ' mph', fallback: '--' },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
      alerts: [
        { op: 'gte', threshold: 25, level: 'advisory' },
        { op: 'gte', threshold: 40, level: 'severe' },
      ],
    }),

    /* Indoor readings, right column: the two entities the user named. Labelled by the static
     * layer, so no text field is needed here — the firmware draws values only. */
    value('ha_hallway', 440, 64, 200, 90, {
      binding: { kind: 'ha', entityId: HA_HALLWAY },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
      alerts: TEMP_ALERTS,
    }),
    value('ha_outdoor', 440, 196, 200, 90, {
      binding: { kind: 'ha', entityId: HA_OUTDOOR },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
      alerts: TEMP_ALERTS,
    }),

    /* Forecast strip, bottom-left: today's high and low, then tomorrow's. */
    value('fc_today_max', 680, 64, 200, 90, {
      binding: { kind: 'owm-daily', dayIndex: 0, owmField: 'max' },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
    }),
    value('fc_today_min', 680, 196, 200, 90, {
      binding: { kind: 'owm-daily', dayIndex: 0, owmField: 'min' },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
    }),
    value('fc_tomorrow_max', 680, 328, 200, 90, {
      binding: { kind: 'owm-daily', dayIndex: 1, owmField: 'max' },
      font: { size: BODY_PX, align: 'left', valign: 'top' },
    }),

    /* The alert bar: full width, bottom. It is bound to OWM official alerts and spans the
     * panel so a warning cannot be mistaken for a reading. */
    value('alerts', 40, 452, 840, 80, {
      binding: { kind: 'owm-alert' },
      showsOwmAlerts: true,
      format: { fallback: '' },
      font: { size: BODY_PX, align: 'left', valign: 'middle' },
    }),
  ];

  /* A second page so the rotation scheduler is exercised by default rather than only when a
   * user happens to add one. It reuses the same boxes at night-appropriate sizes. */
  const page2: Widget[] = [
    value('n_high', 40, 100, 400, 120, {
      binding: { kind: 'owm-daily', dayIndex: 0, owmField: 'max' },
      alerts: TEMP_ALERTS,
    }),
    value('n_low', 40, 280, 400, 120, {
      binding: { kind: 'owm-daily', dayIndex: 0, owmField: 'min' },
      alerts: TEMP_ALERTS,
    }),
    value('n_hallway', 500, 188, 380, 120, {
      binding: { kind: 'ha', entityId: HA_HALLWAY },
      alerts: TEMP_ALERTS,
    }),
  ];

  const now = new Date();
  return {
    schemaVersion: SCHEMA_VERSION,
    generator: 'eink-weather-webapp',
    createdAt: now.toISOString(),
    updateSeconds: 900,
    partialRefreshLimit: 5,
    powerMode: 'auto',
    location: { latitude: 0, longitude: 0, zipCode: '' },
    owmProduct: 'auto',
    ha: { mode: 'rest' },
    pages: [
      { id: 'main', name: 'Weather', refreshSeconds: 900, weight: 1, widgets: page },
      { id: 'forecast', name: 'Forecast', refreshSeconds: 900, weight: 1, widgets: page2 },
    ],
  };
}

/** The static-layer labels for the default layout, for the preview and the golden fixture.
 *  The device never draws text it was not handed values for, so the labels are part of the
 *  LAYOUT the web app bakes into the bitmap. */
export const DEFAULT_LABELS = [
  { x: 40, y: 32, text: 'NOW', font: 0 },
  { x: 440, y: 32, text: 'HALLWAY', font: 0 },
  { x: 680, y: 32, text: 'TODAY HIGH', font: 0 },
  { x: 440, y: 164, text: 'OUTDOOR', font: 0 },
  { x: 680, y: 164, text: 'TODAY LOW', font: 0 },
  { x: 680, y: 296, text: 'TOMORROW HIGH', font: 0 },
] as const;

/** Dividers, inset from the panel edge so they read as section rules. */
export const DEFAULT_RULES = [
  { y: 140, thickness: 2, inset: 40 },
  { y: 440, thickness: 2, inset: 40 },
] as const;

