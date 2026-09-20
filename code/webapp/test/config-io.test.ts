import { describe, it, expect } from 'vitest';
import { exportConfig, importConfig, configFilename } from '../src/transfer/config';
import { emptyConfig, SCHEMA_VERSION } from '../src/model/config';

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

  it('names the file with a date so two saves do not collide', () => {
    expect(configFilename(new Date('2026-09-18T00:00:00Z'))).toMatch(/^eink-weather-\d{4}-\d{2}-\d{2}\.json$/);
  });
});
