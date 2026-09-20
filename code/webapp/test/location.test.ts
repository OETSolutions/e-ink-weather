import { describe, it, expect } from 'vitest';
import {
  parseCoord,
  formatCoord,
  clampCoord,
  coordLabel,
  hasPosition,
} from '../src/ui/location';

describe('coordinate text parsing (FR-24)', () => {
  it('parses a normal latitude and longitude', () => {
    expect(parseCoord('41.7759', 'lat')).toEqual({ ok: true, value: 41.7759 });
    expect(parseCoord('-111.8068', 'lon')).toEqual({ ok: true, value: -111.8068 });
  });

  it('rejects a blank field instead of reading it as 0,0', () => {
    /* Number('') === 0 and Number('   ') === 0, so a naive parse silently turns an empty box
     * into the Gulf of Guinea. That is the whole reason this is strict. */
    expect(parseCoord('', 'lat').ok).toBe(false);
    expect(parseCoord('   ', 'lon').ok).toBe(false);
  });

  it('rejects trailing units rather than guessing the intent', () => {
    expect(parseCoord('41.7N', 'lat').ok).toBe(false);
    expect(parseCoord('41.7 N', 'lat').ok).toBe(false);
  });

  it('rejects values Number would accept loosely', () => {
    expect(parseCoord('Infinity', 'lat').ok).toBe(false);
    expect(parseCoord('0x10', 'lat').ok).toBe(false);
    expect(parseCoord('NaN', 'lat').ok).toBe(false);
  });

  it('enforces the latitude and longitude ranges separately', () => {
    expect(parseCoord('90', 'lat').ok).toBe(true);
    expect(parseCoord('-90', 'lat').ok).toBe(true);
    expect(parseCoord('90.1', 'lat').ok).toBe(false);
    expect(parseCoord('181', 'lon').ok).toBe(false);
    /* 180 is a legal longitude the other side of the antimeridian. */
    expect(parseCoord('180', 'lon').ok).toBe(true);
  });

  it('accepts 0 as a real coordinate (equator / prime meridian)', () => {
    expect(parseCoord('0', 'lat')).toEqual({ ok: true, value: 0 });
  });

  it('formats with the precision the firmware actually sends', () => {
    /* app_refresh.c interpolates %.6f into the OWM URL, so the field must show 6 decimals or
     * it would disagree with what the device fetches with. */
    expect(formatCoord(41.7759)).toBe('41.775900');
    expect(formatCoord(0)).toBe('0.000000');
    expect(formatCoord(-111.8068)).toBe('-111.806800');
  });

  it('round-trips a value through the field without drift', () => {
    const text = formatCoord(41.736512);
    const back = parseCoord(text, 'lat');
    expect(back).toEqual({ ok: true, value: 41.736512 });
  });
});

describe('map coordinate clamping', () => {
  it('clamps latitude at the poles', () => {
    expect(clampCoord(91, 'lat')).toBe(90);
    expect(clampCoord(-95, 'lat')).toBe(-90);
  });

  it('wraps longitude at the antimeridian rather than clamping', () => {
    /* Dragging off the right edge of a world map means "keep going east", not "stop at 180". */
    expect(clampCoord(181, 'lon')).toBe(-179);
    expect(clampCoord(-181, 'lon')).toBe(179);
    expect(clampCoord(190, 'lon')).toBe(-170);
  });
});

describe('position labelling', () => {
  it('labels hemispheres by sign', () => {
    expect(coordLabel(41.7759, 'lat')).toBe('41.7759° N');
    expect(coordLabel(-33.8688, 'lat')).toBe('33.8688° S');
    expect(coordLabel(151.2093, 'lon')).toBe('151.2093° E');
    expect(coordLabel(-111.8068, 'lon')).toBe('111.8068° W');
  });

  it('treats 0,0 as no position, so the map does not open on the Gulf of Guinea', () => {
    /* 0,0 is what both the config default and a failed IP lookup produce. Opening the map
     * there would look like a bug rather than an unset field. */
    expect(hasPosition(0, 0)).toBe(false);
    expect(hasPosition(41.7759, -111.8068)).toBe(true);
    expect(hasPosition(0, 1)).toBe(true);   /* a real place off the prime meridian */
    expect(hasPosition(NaN, 5)).toBe(false);
  });
});
