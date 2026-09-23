/**
 * Home Assistant entity discovery for the picker (FR-23).
 *
 * THE SEARCH RUNS ON THE DISPLAY, NOT IN THE BROWSER. An earlier version called HA's own
 * /api/states from here. That cannot work: Home Assistant sends no CORS headers for an arbitrary
 * origin, so the request is blocked before it is sent — verified on the bench, where a valid
 * token still produced "blocked by CORS policy" and an empty picker, which the user reads as
 * "my HA has no entities". The display holds the HA URL and token and already reaches HA on every
 * refresh, so it runs the search and this only asks it for a term.
 *
 * A SEARCH TERM, NOT THE WHOLE INSTANCE. The device has no PSRAM, so /api/states (1590 entities on
 * the bench instance) would not fit anywhere it could hold it; the device renders a match
 * server-side through HA's template API and returns a bounded list. That also makes the picker
 * fill in as the user types rather than shipping every entity to a dropdown.
 *
 * The point of the picker is to catch a TYPO BEFORE IT SHIPS. A mistyped entity id is accepted
 * by every layer silently — the config stores it, the device asks about it, HA answers "no
 * such entity", and the widget shows its fallback forever with nothing to explain why.
 */

/** Where the device's API lives. The app is served from the device, so a relative URL is right
 *  both on the glass and when the dev server proxies to it. */
const API = '';

/**
 * Search the display's Home Assistant for entities.
 *
 * The device validates the term strictly (it is interpolated into a server-side Jinja template),
 * so a term outside HA's entity-id character set is refused there with an explanation, which is
 * surfaced to the user rather than shown as an empty list.
 */
export async function searchEntities(
  q: string,
  timeoutMs = 12000,
): Promise<{ ok: true; entities: { entityId: string; friendlyName?: string }[]; total: number; truncated: boolean }
          | { ok: false; error: string }> {
  const term = q.trim();
  if (!term) return { ok: false, error: 'Type part of an entity id to search.' };
  const ac = new AbortController();
  const timer = setTimeout(() => ac.abort(), timeoutMs);
  try {
    const r = await fetch(`${API}/api/ha/entities?q=${encodeURIComponent(term)}`, {
      cache: 'no-store', signal: ac.signal,
    });
    clearTimeout(timer);
    const text = await r.text().catch(() => '');
    if (!r.ok) {
      /* Surface the device's own message: it is written for a person, and replacing it with
       * "HTTP 400" would hide the one piece of information that says what to fix. */
      let msg = text;
      try { const j = JSON.parse(text) as { error?: string }; if (j.error) msg = j.error; } catch { /* raw body */ }
      return { ok: false, error: msg || `HTTP ${r.status}` };
    }
    const body = JSON.parse(text) as {
      entities?: { entity_id?: string; friendly_name?: string }[];
      total?: number;
      truncated?: boolean;
    };
    const entities = (body.entities ?? [])
      .filter((e) => typeof e.entity_id === 'string')
      .map((e) => ({ entityId: e.entity_id as string, friendlyName: e.friendly_name }));
    /* `total` is how many matched on the display, which can exceed the rows returned. Falling back
     * to the row count keeps the number honest for a device that does not report one. */
    const total = typeof body.total === 'number' ? body.total : entities.length;
    return { ok: true, entities, total, truncated: !!body.truncated };
  } catch (e) {
    clearTimeout(timer);
    const msg = e instanceof Error ? e.message : 'request failed';
    return {
      ok: false,
      error: /abort/i.test(msg)
        ? `The display did not answer the search within ${timeoutMs / 1000}s`
        : `Could not reach the display to search Home Assistant (${msg})`,
    };
  }
}
