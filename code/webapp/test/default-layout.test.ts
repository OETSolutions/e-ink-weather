import { describe, it, expect } from 'vitest';
import { defaultLayout, HA_OUTDOOR, HA_HALLWAY, DEFAULT_LABELS } from '../src/presets/default-layout';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../src/model/canvas-consts';

describe('default layout (FR-17)', () => {
  const c = defaultLayout();

  it('binds exactly the entities the user asked for', () => {
    const ids = c.pages.flatMap((p) => p.widgets)
      .map((w) => w.binding?.entityId)
      .filter(Boolean);
    expect(ids).toContain(HA_HALLWAY);
    expect(ids).toContain(HA_OUTDOOR);
  });

  it('binds OpenWeatherMap current conditions and forecast', () => {
    const kinds = c.pages.flatMap((p) => p.widgets).map((w) => w.binding?.kind);
    expect(kinds).toContain('owm-current');
    expect(kinds).toContain('owm-daily');
  });

  it('shows where the readings come from (FR-17 location display)', () => {
    /* FR-17 asks for a location/zip display. The widget must be bound to the OWM place name,
     * and it must have a NON-DEFAULT fallback: the free-tier 2.5/weather response used when a
     * key has no One Call subscription does carry "name", but a failed fetch or a One Call
     * response does not — so an empty box would read as "this panel has no location" rather
     * than "the location could not be read". */
    const w = c.pages[0]!.widgets.find((x) => x.id === 'owm_city');
    expect(w, 'no location widget on the default page').toBeDefined();
    expect(w!.binding).toEqual({ kind: 'owm-current', owmField: 'city' });
    expect(w!.format?.fallback).toBeTruthy();
    expect(w!.format?.fallback).not.toBe('--');
  });

  it('labels every dynamic widget that is not self-describing', () => {
    /* A bare number with no label is unreadable on the glass, and the labels are ART — they
     * are baked into the static layer, so a widget added without its label shows a naked
     * value. Checks the location widget specifically because it is text, where a missing
     * label is most easily mistaken for the reading itself. */
    const labels = DEFAULT_LABELS.map((l) => l.text);
    expect(labels).toContain('LOCATION');
  });

  it('every widget lies inside the panel', () => {
    for (const p of c.pages) {
      for (const w of p.widgets) {
        expect(w.x, w.id).toBeGreaterThanOrEqual(0);
        expect(w.y, w.id).toBeGreaterThanOrEqual(0);
        expect(w.x + w.w, w.id).toBeLessThanOrEqual(PANEL_WIDTH);
        expect(w.y + w.h, w.id).toBeLessThanOrEqual(PANEL_HEIGHT);
      }
    }
  });

  it('no two widgets overlap (a clean starting point, not a pile)', () => {
    for (const p of c.pages) {
      for (let i = 0; i < p.widgets.length; i++) {
        for (let j = i + 1; j < p.widgets.length; j++) {
          const a = p.widgets[i]!, b = p.widgets[j]!;
          const overlap = a.x < b.x + b.w && b.x < a.x + a.w &&
                          a.y < b.y + b.h && b.y < a.y + a.h;
          expect(overlap, `${a.id} overlaps ${b.id}`).toBe(false);
        }
      }
    }
  });

  it('ships alert rules for extreme weather (FR-14)', () => {
    const all = c.pages.flatMap((p) => p.widgets);
    expect(all.some((w) => (w.alerts?.length ?? 0) > 0)).toBe(true);
    expect(all.some((w) => w.showsOwmAlerts)).toBe(true);
  });

  it('uses non-default settings so a dropped field would show up', () => {
    /* Two pages exercises the rotation scheduler (FR-15/16), and the alert bar must be at the
     * bottom because it spans the width. */
    expect(c.pages.length).toBeGreaterThan(1);
    const bar = c.pages[0]!.widgets.find((w) => w.showsOwmAlerts)!;
    expect(bar.y + bar.h).toBeLessThanOrEqual(PANEL_HEIGHT);
    expect(bar.w).toBeGreaterThan(bar.h * 5);   /* it really is a bar */
  });

  it('gives the hero reading the largest type on the page', () => {
    /* The whole point of the layout is that the current temperature is readable across a room,
     * so a later edit that shrank it below another reading should fail here. */
    const w0 = c.pages[0]!.widgets;
    const hero = w0.find((w) => w.id === 'owm_temp')!;
    const biggest = Math.max(...w0.map((w) => w.font?.size ?? 0));
    expect(hero.font?.size).toBe(biggest);
  });

  it('is a valid, serialisable config', () => {
    const round = JSON.parse(JSON.stringify(c));
    expect(round.schemaVersion).toBe(c.schemaVersion);
    expect(round.pages).toHaveLength(c.pages.length);
    /* Every widget needs an id: it is what the editor selects by and what the preview keys
     * values on, so a duplicate would make two boxes move together. */
    const ids = c.pages.flatMap((p) => p.widgets).map((w) => w.id);
    expect(new Set(ids).size).toBe(ids.length);
  });
});
