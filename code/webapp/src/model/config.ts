/**
 * The canonical configuration document (IF-1).
 *
 * This is the single source of truth shared by the web app and the firmware.
 * The firmware parses only a subset (see layout_config_t) but the document as a
 * whole lives here, is versioned, and must survive round-tripping through a
 * saved file.
 */

export const SCHEMA_VERSION = 1;

export { PANEL_WIDTH, PANEL_HEIGHT, FB_BYTES } from './canvas-consts';

export type AlertOp = 'gt' | 'gte' | 'lt' | 'lte' | 'eq' | 'ne';
export type AlertLevel = 'none' | 'advisory' | 'warning' | 'severe';

export type PowerMode = 'auto' | 'always-on' | 'battery';

export type DataSourceKind = 'owm-current' | 'owm-daily' | 'owm-alert' | 'ha';

export interface DataBinding {
  kind: DataSourceKind;
  /** For 'ha', the HA entity id, e.g. "sensor.upstairs_hallway_temperature". */
  entityId?: string;
  /** For 'owm-daily', the 0-based forecast day and which value. */
  dayIndex?: number;
  owmField?: 'temp' | 'min' | 'max' | 'wind' | 'humidity' | 'condition' | 'icon';
}

export interface AlertRule {
  op: AlertOp;
  threshold: number;
  level: Exclude<AlertLevel, 'none'>;
}

/** How a bound value is rendered as text. Extracted as a named type so the formatter can
 *  take it without reaching into Widget["format"]. */
export interface Format {
  decimals?: number;
  prefix?: string;
  suffix?: string;
  fallback?: string;
}

export interface Widget {
  id: string;
  x: number;
  y: number;
  w: number;
  h: number;
  /** 'static' is baked into the pre-rendered bitmap; 'dynamic' is redrawn each refresh. */
  role: 'static' | 'dynamic';
  binding?: DataBinding;
  /** How to present a bound value. */
  format?: Format;
  font?: { size: number; align: 'left' | 'center' | 'right'; valign: 'top' | 'middle' | 'bottom' };
  alerts?: AlertRule[];
  /** Present for alert-bar widgets driven by OWM official alerts. */
  showsOwmAlerts?: boolean;
}

export interface Page {
  id: string;
  name: string;
  refreshSeconds: number;
  weight: number;
  widgets: Widget[];
}

export interface LocationConfig {
  /** Precise position picked on the map. */
  latitude: number;
  longitude: number;
  /** General position for display / fallback. */
  zipCode: string;
}

export interface Config {
  schemaVersion: number;
  generator: string;
  createdAt: string;
  updateSeconds: number;
  partialRefreshLimit: number;
  /**
   * The FR-8 power behaviour override.
   *
   * 'auto' accepts the device's VBAT inference, which is fallible: this board has no
   * resolvable USB-present pin, so a full resting cell sits above the mains threshold and is
   * indistinguishable from USB at the moment of measurement. The UI must present this as a
   * three-way control and label 'auto' as *inferred*, so the user knows when they are
   * trusting a guess rather than correcting one.
   */
  powerMode: PowerMode;
  location: LocationConfig;
  /** Which OWM product to use; 'auto' probes One Call 3.0 and falls back (FR-6). */
  owmProduct: 'auto' | 'onecall3' | 'legacy';
  ha: {
    mode: 'rest' | 'mqtt' | 'off';
    /** Base URL of the HA instance, e.g. http://homeassistant.local:8123. Blank means "not
     *  configured", which the entity picker reports rather than showing an empty list. */
    baseUrl?: string;
    baseTopic?: string;
  };
  pages: Page[];
}

export function emptyConfig(now: Date = new Date()): Config {
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
    pages: [{ id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [] }],
  };
}
