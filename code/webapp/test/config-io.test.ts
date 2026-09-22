import { describe, it, expect } from 'vitest';
import { exportConfig, importConfig, configFilename } from '../src/transfer/config';
import { readFileSync } from 'node:fs';
import { emptyConfig, SCHEMA_VERSION, MAX_WIDGETS_PER_PAGE, MAX_ALERT_RULES_PER_WIDGET } from '../src/model/config';

describe('config file I/O (FR-26)', () => {
  it('round-trips a config through a saved file', () => {
    const c = emptyConfig();
    expect(importConfig(exportConfig(c))).toEqual(c);
  });

  it('round-trips a NON-DEFAULT value, so a field cannot be silently dropped', () => {
    const c = emptyConfig();
    c.powerMode = 'battery';
    c.location = { latitude: 41.7759, longitude: -111.8068, zipCode: '84341' };
    c.pages[0]!.widgets.push({
      id: 'w1', x: 10, y: 20, w: 300, h: 120, role: 'dynamic',
      binding: { kind: 'ha', entityId: 'sensor.x' },
      format: { decimals: 1, suffix: '°F' },
      alerts: [{ op: 'gt', threshold: 100, level: 'severe' }],
    });
    const back = importConfig(exportConfig(c));
    expect(back).toEqual(c);
    expect(back.powerMode).toBe('battery');
    expect(back.pages[0]!.widgets[0]!.binding?.entityId).toBe('sensor.x');
  });

  it('embeds schemaVersion and a timestamp so a file is self-describing (FR-26b)', () => {
    const text = exportConfig(emptyConfig(new Date('2026-09-18T12:00:00Z')));
    const parsed = JSON.parse(text);
    expect(parsed.schemaVersion).toBe(SCHEMA_VERSION);
    expect(parsed.createdAt).toBe('2026-09-18T12:00:00.000Z');
    expect(parsed.generator).toBe('eink-weather-webapp');
  });

  it('refuses a file from a newer version instead of mis-reading it (FR-26b)', () => {
    expect(() => importConfig('{"schemaVersion": 99, "pages": []}')).toThrow(/newer/i);
  });

  it('refuses a document with no version rather than assuming the current one', () => {
    expect(() => importConfig('{"pages": []}')).toThrow(/version/i);
  });

  it('refuses malformed input', () => {
    expect(() => importConfig('not json')).toThrow();
    expect(() => importConfig('[]')).toThrow();
    expect(() => importConfig('null')).toThrow();
  });

  /* An OLDER file — saved before a field existed — must still load, with the new field taking
   * its default. Refusing it would make every saved layout expire when the schema grows. */
  it('accepts an older document and fills in the new defaults', () => {
    const old = JSON.stringify({ schemaVersion: 1, pages: [{ name: 'Old', refreshSeconds: 900, weight: 1, widgets: [] }] });
    const c = importConfig(old);
    expect(c.powerMode).toBe('auto');       /* absent in the file */
    expect(c.pages[0]!.name).toBe('Old');
    expect(c.location).toBeDefined();
    expect(c.ha).toBeDefined();
  });

  /* Pages that carry no widgets array are the device's own shape, and must not crash a reader. */
  it('gives a widget-less page an empty array', () => {
    const c = importConfig(JSON.stringify({ schemaVersion: 1, pages: [{ name: 'P' }] }));
    expect(Array.isArray(c.pages[0]!.widgets)).toBe(true);
  });

  /* RULES. They are part of the document so the user can move a divider, which means a saved
   * file must carry them back — a line that vanished on reload would be a divider the user
   * cannot keep. A page with no `rules` key must still load (an older file), which is why the
   * field is optional rather than defaulted to a non-empty array. */
  it('round-trips dividers with their own geometry', () => {
    const c = emptyConfig();
    c.pages[0]!.rules = [{ y: 236, thickness: 3, inset: 60 }];
    const back = importConfig(exportConfig(c));
    expect(back.pages[0]!.rules).toEqual([{ y: 236, thickness: 3, inset: 60 }]);
  });

  it('leaves rules absent when the file has none, rather than inventing dividers', () => {
    const c = importConfig(JSON.stringify({
      schemaVersion: 1, pages: [{ name: 'P', widgets: [] }],
    }));
    expect(c.pages[0]!.rules).toBeUndefined();
  });

  it('names the file with a date so two saves do not collide', () => {
    expect(configFilename(new Date('2026-09-18T00:00:00Z'))).toMatch(/^eink-weather-\d{4}-\d{2}-\d{2}\.json$/);
  });
});

/* THE DEVICE'S LIMITS AND THE EDITOR'S MUST AGREE — in the direction that matters.
 *
 * The editor refuses to add a box past MAX_WIDGETS_PER_PAGE and a 7th alert rule per widget,
 * because the device cannot honour more. If these ever drift, a layout the editor accepts would
 * be silently truncated on the glass or refused at save time — the failure mode this project
 * keeps hitting, where the UI confirms a change that has no effect on the panel.
 *
 * Read from the firmware header itself rather than a copied literal, so a change on either side
 * fails here instead of on the device. */
describe('webapp and firmware agree on the device limits', () => {
  const header = readFileSync(
    new URL('../../firmware/lib/layout/include/widgets.h', import.meta.url), 'utf8',
  );

  it('never allows more widgets than the firmware will parse', () => {
    /* A cap ABOVE LAYOUT_MAX_FIELDS would let a user build a page the device silently truncates.
     * A cap BELOW it is deliberate — see MAX_WIDGETS_PER_PAGE: the binding limit is the cJSON
     * tree fitting memory, which is tighter than the parser's array size. */
    const m = /#define\s+LAYOUT_MAX_FIELDS\s+(\d+)/.exec(header);
    expect(m, 'LAYOUT_MAX_FIELDS not found in widgets.h').not.toBeNull();
    expect(MAX_WIDGETS_PER_PAGE).toBeLessThanOrEqual(Number(m![1]));
  });

  it('caps alert rules per widget identically', () => {
    const m = /#define\s+LAYOUT_MAX_RULES\s+(\d+)/.exec(header);
    expect(m, 'LAYOUT_MAX_RULES not found in widgets.h').not.toBeNull();
    expect(Number(m![1])).toBe(MAX_ALERT_RULES_PER_WIDGET);
  });
});
