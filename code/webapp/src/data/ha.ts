/**
 * Home Assistant entity discovery (FR-23).
 *
 * BROWSER ONLY, AND THAT IS A REAL DIFFERENCE FROM THE DEVICE. The plan is explicit: this uses
 * `GET /api/states` — ONE call that returns every entity — because the browser has the memory
 * for it and the user needs a list to choose from. The device path cannot do that: it has no
 * PSRAM and its config body is capped at 16 KB, so it asks about one entity at a time through
 * the template API. Using the same call in both places would not work on the glass.
 *
 * The point of the picker is to catch a TYPO BEFORE IT SHIPS. A mistyped entity id is accepted
 * by every layer silently — the config stores it, the device asks about it, HA answers "no
 * such entity", and the widget shows its fallback forever with nothing to explain why. One
 * validation here, at the moment the user types it, is the whole feature.
 */

export interface HaEntity {
  entityId: string;
  state: string;
  /** HA's own name for the entity, for showing something friendlier than the id. */
  friendlyName?: string;
}

export interface HaConfig {
  /** Base URL of the HA instance, e.g. http://homeassistant.local:8123 */
  baseUrl: string;
  token: string;
}

/** Where the device's API lives; the browser reaches HA through the same origin it was served
 *  from. HA itself is NOT proxied — see the CORS note below. */
const API = '';

export type EntityListResult =
  | { ok: true; entities: HaEntity[] }
  | { ok: false; error: string };

/**
 * List every HA entity.
 *
 * THE TOKEN DOES NOT TRAVEL FROM HERE. In the intended deployment the *device* holds the HA
 * token (in NVS, entered once during provisioning) and does the talking; the browser only asks
 * HA directly when the user is configuring from the same LAN. So the caller supplies the token
 * rather than this reading it from anywhere.
 *
 * A CORS OR NETWORK FAILURE IS REPORTED AS SUCH, NOT AS "no entities". Home Assistant does not
 * send CORS headers for arbitrary origins by default, so this call legitimately fails in some
 * setups — and a silent empty list would look like "your HA has no entities", which is the
 * worst possible message. The caller can then still accept a typed id.
 */
export async function listEntities(cfg: HaConfig, timeoutMs = 8000): Promise<EntityListResult> {
  if (!cfg || !cfg.baseUrl) return { ok: false, error: 'No Home Assistant URL configured' };

  const url = `${cfg.baseUrl.replace(/\/+$/, '')}/api/states`;
  const ac = new AbortController();
  const timer = setTimeout(() => ac.abort(), timeoutMs);
  try {
    const r = await fetch(url, {
      headers: { Authorization: `Bearer ${cfg.token ?? ''}` },
      signal: ac.signal,
    });
    clearTimeout(timer);
    if (r.status === 401 || r.status === 403) {
      return { ok: false, error: 'Home Assistant rejected the access token' };
    }
    if (!r.ok) return { ok: false, error: `Home Assistant returned HTTP ${r.status}` };

    const raw = (await r.json()) as unknown;
    if (!Array.isArray(raw)) return { ok: false, error: 'Unexpected reply from Home Assistant' };

    const entities: HaEntity[] = [];
    for (const e of raw) {
      const o = e as { entity_id?: unknown; state?: unknown; attributes?: { friendly_name?: unknown } };
      if (typeof o.entity_id !== 'string') continue;
      const fn = o.attributes?.friendly_name;
      entities.push({
        entityId: o.entity_id,
        state: typeof o.state === 'string' ? o.state : '',
        friendlyName: typeof fn === 'string' ? fn : undefined,
      });
    }
    return { ok: true, entities };
  } catch (e) {
    clearTimeout(timer);
    const msg = e instanceof Error ? e.message : 'request failed';
    /* Distinguish a timeout from the common CORS wall, because the fixes differ: a timeout is
     * the address or the network, CORS is a Home Assistant configuration change. */
    if (msg.toLowerCase().includes('abort')) {
      return { ok: false, error: `No reply from ${url} within ${timeoutMs / 1000}s` };
    }
    return {
      ok: false,
      error: `Could not reach Home Assistant (${msg}). If the address is right, HA may be ` +
             `refusing cross-origin requests — the device can still use the entity by name.`,
    };
  }
}

/**
 * Does this entity exist? Returns null when it cannot be determined (HA unreachable), which is
 * DIFFERENT from false (HA answered and has no such entity).
 *
 * That distinction matters for the UI: false is a typo the user must fix, null is "cannot
 * check right now, accept it". Collapsing them would either block a valid id or wave a typo
 * through, and the user cannot tell which happened.
 */
export async function validateEntity(
  cfg: HaConfig,
  entityId: string,
  entities?: HaEntity[],
): Promise<boolean | null> {
  if (!entityId) return false;

  /* Reuse a list the caller already fetched, so a panel that has one does not re-request per
   * keystroke. */
  if (entities) return entities.some((e) => e.entityId === entityId);

  const res = await listEntities(cfg);
  if (!res.ok) return null;
  return res.entities.some((e) => e.entityId === entityId);
}

/** The device's own status, used to tell whether it has an HA token to configure against. */
export async function deviceHaConfig(): Promise<Partial<HaConfig>> {
  try {
    const r = await fetch(`${API}/api/config`, { cache: 'no-store' });
    if (!r.ok) return {};
    const doc = (await r.json()) as { ha?: { baseUrl?: string } };
    return { baseUrl: doc.ha?.baseUrl };
  } catch {
    return {};
  }
}
