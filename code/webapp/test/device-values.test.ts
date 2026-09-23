import { describe, it, expect, vi, afterEach } from 'vitest';
import { getValuesInfo } from '../src/transfer/device';
import { previewTextWithLive } from '../src/data/format';

/**
 * The device -> editor handoff for a live reading (FR-27).
 *
 * THIS FILE EXISTS FOR ONE REAL BUG. The endpoint reports `rendered_number` as a NUMBER (the
 * firmware writes 1/0), and the app tested it with `=== true`. That silently never matched, so the
 * raw reading was fetched and then dropped and the preview went on echoing the format the device
 * had used — the exact "the editor doesn't show the updated values unless you first save and
 * refresh" symptom the field was added to fix. It was invisible to the unit tests (which build the
 * live map by hand) and to any code review (the types said boolean), and only showed up in a real
 * browser against the device.
 *
 * The shape below is copied from the device's actual response.
 */

/* jsonCall() reads the body with r.text() and parses it itself (so it can surface the device's own
 * error message), so the stub must provide text(), not json(). */
const json = (body: unknown) =>
  ({ ok: true, status: 200, text: async () => JSON.stringify(body) }) as unknown as Response;

afterEach(() => vi.unstubAllGlobals());

describe('getValuesInfo carries the raw reading the device resolved', () => {
  it('keeps the value when the device flags the text as a number rendering', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => json({
      page: 0, drawn_page: 0, page_count: 2, resolved_at: 5,
      values: [{ id: 'owm_temp', text: '60.6°F', has_value: 1, rendered_number: 1, value: 60.57 }],
    })));
    const info = await getValuesInfo({ baseUrl: 'http://dev' }, 0);
    /* The number must survive, and it must survive TRUTHINESS — the firmware sends 1, so a
     * boolean-only test would drop it. */
    expect(info?.values['owm_temp']).toEqual({ text: '60.6°F', value: 60.57 });
  });

  it('DROPS the value for a text reading, so the editor cannot invent a number', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => json({
      page: 0, drawn_page: 0, page_count: 1, resolved_at: 5,
      values: [
        { id: 'owm_cond', text: 'clear sky', has_value: 1, rendered_number: 0, value: 0 },
        { id: 'owm_icon', text: '01d', has_value: 1, rendered_number: 0, value: 0 },
        { id: 'alerts', text: 'severe', has_value: 1, rendered_number: 0, value: 0 },
      ],
    })));
    const info = await getValuesInfo({ baseUrl: 'http://dev' }, 0);
    expect(info?.values['owm_cond']).toEqual({ text: 'clear sky' });
    expect(info?.values['owm_icon']).toEqual({ text: '01d' });
    expect(info?.values['alerts']).toEqual({ text: 'severe' });
    /* ...and the editor echoes them rather than formatting a 0 into "0.0°F". */
    const w = { id: 'owm_cond', role: 'dynamic', format: { decimals: 1, suffix: '°F' } };
    expect(previewTextWithLive(w, NaN, info!.values)).toBe('clear sky');
  });

  it('keeps a ZERO reading, which is a real temperature', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => json({
      page: 0, drawn_page: 0, page_count: 1, resolved_at: 5,
      values: [{ id: 't', text: '0.0°F', has_value: 1, rendered_number: 1, value: 0 }],
    })));
    const info = await getValuesInfo({ baseUrl: 'http://dev' }, 0);
    expect(info?.values['t']).toEqual({ text: '0.0°F', value: 0 });
  });

  /* END TO END THROUGH ONE REAL RESPONSE: fetch the values, then preview with an EDIT the user just
   * made. This is the chain the reported defect lived in, and the assertion is on the final string —
   * the thing that reaches the canvas. */
  it('re-formats a fetched reading with the format the user is editing', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => json({
      page: 0, drawn_page: 0, page_count: 1, resolved_at: 5,
      values: [{ id: 'owm_temp', text: '60.6°F', has_value: 1, rendered_number: 1, value: 60.57 }],
    })));
    const info = await getValuesInfo({ baseUrl: 'http://dev' }, 0);
    /* The device drew "60.6°F" with 1 decimal; the user has just switched to 0 and added a prefix. */
    const w = { id: 'owm_temp', role: 'dynamic', format: { decimals: 0, prefix: 'NOW ', suffix: '°F' } };
    expect(previewTextWithLive(w, NaN, info!.values)).toBe('NOW 61°F');
  });

  it('returns null when the device is unreachable (distinct from "no values")', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 500, text: async () => 'boom' }) as unknown as Response));
    expect(await getValuesInfo({ baseUrl: 'http://dev' }, 0)).toBeNull();
  });
});
