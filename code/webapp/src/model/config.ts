/**
 * The canonical configuration document (IF-1).
 *
 * This is the single source of truth shared by the web app and the firmware.
 * The firmware parses only a subset (see layout_config_t) but the document as a
 * whole lives here, is versioned, and must survive round-tripping through a
 * saved file.
 */

export const SCHEMA_VERSION = 1;

/**
 * The largest page the device reliably STORES.
 *
 * WHY THIS IS BELOW THE FIRMWARE'S LAYOUT_MAX_FIELDS (24) AND IS NOT A MIRROR OF IT: the
 * firmware's constant bounds how many widgets the layout PARSER will read, but the binding limit
 * is memory. A PUT parses the document with cJSON, whose tree is several times the document's
 * size, and on this part (no PSRAM, one big DRAM region split by the httpd and app task stacks)
 * the largest free block runs 11-22 KB. Measured on hardware: a page of 21 widgets (7.3 KB)
 * stores every time, and 22 (7.5 KB) is refused every time — with "invalid config", because
 * cJSON_Parse returns NULL for an allocation failure exactly as it does for a malformed document.
 *
 * So the editor caps at the number the device can actually persist. A cap at 24 would let a user
 * build a layout the editor shows in full and the device then refuses to save, with a message
 * blaming their document. 20 leaves margin below the measured 21/22 cliff for a busier heap.
 */
export const MAX_WIDGETS_PER_PAGE = 20;

/**
 * The device's per-widget alert-rule cap, mirrored from LAYOUT_MAX_RULES in
 * lib/layout/include/widgets.h.
 *
 * THIS ONE IS A TRUE MIRROR: the rules are part of each widget's own parsed structure, not extra
 * top-level tokens, so the limit really is the device's array size. The editor enforces it so the
 * limit is visible while editing rather than discovered on the panel — the device drops the
 * overflow without a word.
 */
export const MAX_ALERT_RULES_PER_WIDGET = 6;

export { PANEL_WIDTH, PANEL_HEIGHT, FB_BYTES } from './canvas-consts';

export type AlertOp = 'gt' | 'gte' | 'lt' | 'lte' | 'eq' | 'ne';
export type AlertLevel = 'none' | 'advisory' | 'warning' | 'severe';

export type PowerMode = 'auto' | 'always-on' | 'battery';

export type DataSourceKind = 'owm-current' | 'owm-daily' | 'owm-alert' | 'ha' | 'image';

export interface DataBinding {
  kind: DataSourceKind;
  /** For 'ha', the HA entity id, e.g. "sensor.upstairs_hallway_temperature". */
  entityId?: string;
  /** For 'owm-daily', the 0-based forecast day and which value. */
  dayIndex?: number;
  /** `time` is the "last updated" stamp: OWM's own observation time for the current reading,
   *  rendered in the location's local clock. Current-only — a forecast document has no observation
   *  time, so the field is absent. */
  owmField?: 'temp' | 'min' | 'max' | 'wind' | 'humidity' | 'condition' | 'icon' | 'city' | 'time';
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

/**
 * The longest prefix or suffix the DEVICE can store, in characters.
 *
 * WHY THIS IS MIRRORED HERE AND NOT JUST IN THE FIRMWARE: the device keeps a format's affixes in
 * fixed arrays, and they used to hold only five characters — so a prefix of "layers " was cut to
 * "layer" on the glass while the app's own preview (which has no such limit) showed "layers 0".
 * The two disagreed silently, and the user only saw it by reading the panel.
 *
 * Bounding the field to what the device can store removes the disagreement at the source: the
 * user cannot type something that will be trimmed. It matches widget_format_t.prefix/.suffix in
 * lib/layout/include/widgets.h — if that grows, this must grow with it.
 */
export const MAX_AFFIX = 15;

/** The longest fallback the device can store. Matches widget_format_t.fallback. */
export const MAX_FALLBACK = 7;
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

/**
 * A horizontal divider in the static art.
 *
 * IT IS PART OF THE CONFIG, not the web app's own table, because the user must be able to move
 * it: a rule that can only be nudged by editing a preset is a line the user does not control.
 * The device never sees a rule as a rule — rules are rasterised into the 1-bpp artwork this app
 * uploads — but they still travel in the document so a saved layout round-trips and a GET hands
 * back the lines the user actually drew. The firmware's layout parser ignores an unknown
 * per-page key, and PUT stores the whole body verbatim, so a rule survives the round-trip
 * without a firmware change.
 *
 * `inset` is measured from BOTH panel edges, so a rule spans x=inset .. PANEL_WIDTH-inset.
 */
export interface Rule {
  y: number;
  thickness: number;
  inset: number;
}

/**
 * A static text label: "NOW", "HALLWAY", "TODAY HIGH".
 *
 * IT IS PART OF THE CONFIG, not the web app's own table, for the same reason a Rule is: the user
 * must be able to rename and move it. These labels used to be a hard-coded per-page table, so the
 * headings on the glass were the headings the preset happened to contain — a user who wanted
 * "COOP" instead of "OUTDOOR", or a heading where there was none, had no way to get one short of
 * hand-editing JSON.
 *
 * The device never sees a label: labels are rasterised into the 1-bpp artwork this app uploads,
 * and the firmware draws only values (FR-1). But they still travel in the document so a saved
 * layout round-trips and a GET hands back the labels the user actually wrote. The firmware's
 * layout parser ignores unknown per-page keys, so a label survives the round-trip unchanged.
 *
 * THERE IS NO WIDTH OR HEIGHT. A label is one line of text; its box is measured from the glyphs
 * (see canvas/geometry.ts:labelRect), so a width stored here could only drift out of step with
 * the text it is supposed to bound.
 */
export interface Label {
  x: number;
  y: number;
  text: string;
  /** Pixel size; the ladder resolves it to a face, exactly like a widget's font.size. */
  font: number;
}

/** A picture the user uploaded into a box.
 *
 *  AN IMAGE BOX IS A WIDGET whose binding kind is 'image' and whose role is 'static'. That is not
 *  a trick: the pixels ARE static content, baked into the page's artwork exactly like a heading or
 *  a divider, and the firmware already skips a static widget — so an image box needs NO firmware
 *  support at all. The widget's x/y/w/h is the placement, and the raster is produced at that exact
 *  size, so the artwork needs no scaling at draw time and the preview matches the glass.
 *
 *  THE PIXELS DO NOT LIVE IN THE CONFIG. The device is a 1-bit ink-only panel with no image
 *  decoder, so the app rasterises the picture to black-and-white and bakes it into the page's
 *  artwork (FR-1: the device stamps VALUES, the app draws everything else). The ORIGINAL image is
 *  held by the browser (see ui/image-store.ts) so the box can be resized and re-dithered.
 *
 *  WHY NOT THE PIXELS IN THE CONFIG, which would make the picture travel with the layout: the
 *  device re-parses the whole document on EVERY refresh tick, inside the render window where the
 *  largest free block falls to ~1.7 KB, and a PUT is already refused with `oom` above ~12 KB
 *  (measured). A rasterised picture is 3-20 KB compressed, so carrying it here would push every
 *  refresh through an allocation that has already failed on this board. The artwork partition
 *  exists for exactly this and is read only when a layer is drawn. */
export interface ImageBinding {
  kind: 'image';
}

export interface Page {
  id: string;
  name: string;
  refreshSeconds: number;
  weight: number;
  widgets: Widget[];
  /** Dividers, in the static layer (FR-15). Absent means none. */
  rules?: Rule[];
  /** Static text labels, in the static layer. Absent on a document authored before labels moved
   *  into the config; the shell seeds such a page from the art table (see ensurePageLabels). */
  labels?: Label[];
}

/** The id every rule shares. A rule is not a widget, but the editor selects, drags and deletes
 *  one by id like a widget, so it carries the same field with a constant value. A rule can
 *  never be a widget: its id is reserved here and never assigned to a value box. */
export const RULE_ID = '@rule';

/** The id every label shares. Reserved like RULE_ID, and for the same reason: a label is selected
 *  and deleted like a widget but is addressed by INDEX, so it must never collide with a widget
 *  id. A label can never be a widget. */
export const LABEL_ID = '@label';

/** A rule as the editor addresses it: an id plus the divider. */
export interface RuleSelection extends Rule {
  id: typeof RULE_ID;
  index: number;
}

/** A label as the editor addresses it: an id plus the label and its index in the page. */
export interface LabelSelection extends Label {
  id: typeof LABEL_ID;
  index: number;
}

/** The editor's selection is a widget (by id), a rule (by id AND index), or a label. */
export type Selection =
  | { kind: 'widget'; id: string }
  | { kind: 'rule'; id: string; index: number }
  | { kind: 'label'; id: string; index: number };

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
  /**
   * FR-33: let the device install newer GitHub releases on its own, on USB power at boot.
   *
   * A JSON BOOLEAN only — the firmware treats anything else (including the string "true") as OFF,
   * because this decides whether the device replaces its own firmware unattended, so it must
   * default to OFF and only an explicit tick enables it. Absent means OFF.
   */
  firmwareAutoUpdate?: boolean;
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
    pages: [{ id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [], rules: [] }],
  };
}
