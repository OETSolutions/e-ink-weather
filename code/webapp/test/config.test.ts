import { describe, it, expect } from 'vitest';
import {
  emptyConfig, SCHEMA_VERSION, PANEL_WIDTH, PANEL_HEIGHT, FB_BYTES,
} from '../src/model/config';

describe('config model', () => {
  it('framebuffer math matches the panel contract (HW-6)', () => {
    expect(PANEL_WIDTH).toBe(920);
    expect(PANEL_HEIGHT).toBe(680);
    expect(FB_BYTES).toBe(78_200);
    expect(FB_BYTES).toBe((PANEL_WIDTH * PANEL_HEIGHT) / 8);
  });

  it('a fresh config is valid and has one page', () => {
    const c = emptyConfig(new Date('2026-09-18T00:00:00Z'));
    expect(c.schemaVersion).toBe(SCHEMA_VERSION);
    expect(c.pages).toHaveLength(1);
    expect(c.pages[0]!.name).toBe('Main');
  });

  it('defaults are the documented ones', () => {
    const c = emptyConfig();
    expect(c.updateSeconds).toBe(900);
    expect(c.partialRefreshLimit).toBe(5);
    expect(c.owmProduct).toBe('auto');
    expect(c.ha.mode).toBe('rest');
    expect(c.powerMode).toBe('auto');
  });

  it('round-trips a NON-DEFAULT powerMode, so the field cannot be silently dropped (FR-8)', () => {
    const c = emptyConfig();
    c.powerMode = 'battery';
    const back = JSON.parse(JSON.stringify(c));
    expect(back.powerMode).toBe('battery');
    expect(back).toEqual(c);
  });

  it('round-trips through JSON without loss', () => {
    const c = emptyConfig();
    c.pages[0]!.widgets.push({
      id: 'w1', x: 10, y: 20, w: 300, h: 120, role: 'dynamic',
      binding: { kind: 'ha', entityId: 'sensor.upstairs_hallway_temperature' },
      format: { decimals: 1, suffix: '°F' },
      font: { size: 48, align: 'left', valign: 'middle' },
      alerts: [{ op: 'gt', threshold: 100, level: 'severe' }],
    });
    const back = JSON.parse(JSON.stringify(c));
    expect(back).toEqual(c);
  });
});
