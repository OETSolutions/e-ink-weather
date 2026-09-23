/**
 * The device HTTP client (IF-4).
 *
 * Every call goes through here so that two things are true in exactly one place: the optional
 * bearer token is attached when the device has auth enabled, and an error carries the device's
 * own message rather than a bare status code. The device is unusually informative about why it
 * refused something ("invalid config: needs numeric schemaVersion and a valid layout"), and
 * throwing that away makes a user-facing bug report impossible to act on.
 *
 * A SHORT TIMEOUT ON EVERY CALL, deliberately. Without one, a device that has dropped off the
 * wifi makes fetch() HANG rather than fail, and every caller then sits waiting with no error —
 * which reads as "the app is broken" instead of "the device is unreachable".
 */

export interface DeviceOptions {
  baseUrl?: string;
  token?: string;
  timeoutMs?: number;
}

const DEFAULT_TIMEOUT_MS = 8000;

export interface DeviceStatus {
  vbat?: number;
  power_source?: string;
  last_refresh?: unknown;
  rssi?: number;
  errors?: string[];
  [k: string]: unknown;
}

export type JsonResult<T> = { ok: true; value: T } | { ok: false; error: string; status?: number };

async function request(
  path: string,
  init: RequestInit,
  opts: DeviceOptions,
): Promise<{ r: Response | null; error?: string }> {
  const ac = new AbortController();
  const timer = setTimeout(() => ac.abort(), opts.timeoutMs ?? DEFAULT_TIMEOUT_MS);
  try {
    const headers: Record<string, string> = {
      ...((init.headers as Record<string, string>) ?? {}),
    };
    if (opts.token) headers.Authorization = `Bearer ${opts.token}`;
    const r = await fetch(`${opts.baseUrl ?? ''}${path}`, { ...init, headers, signal: ac.signal });
    return { r };
  } catch (e) {
    return {
      r: null,
      error: e instanceof Error && e.name === 'AbortError'
        ? `The device did not answer within ${(opts.timeoutMs ?? DEFAULT_TIMEOUT_MS) / 1000}s`
        : `Could not reach the device (${e instanceof Error ? e.message : 'network error'})`,
    };
  } finally {
    clearTimeout(timer);
  }
}

async function jsonCall<T>(path: string, init: RequestInit, opts: DeviceOptions): Promise<JsonResult<T>> {
  const { r, error } = await request(path, init, opts);
  if (!r) return { ok: false, error: error ?? 'request failed' };

  const text = await r.text().catch(() => '');
  if (!r.ok) {
    /* Surface the device's message. It is written for a person, and replacing it with
     * "HTTP 400" loses the one piece of information that says what to fix. */
    let msg = text;
    try {
      const j = JSON.parse(text) as { error?: string };
      if (j.error) msg = j.error;
    } catch { /* not JSON: use the raw body */ }
    return { ok: false, error: msg || `HTTP ${r.status}`, status: r.status };
  }
  if (!text) return { ok: true, value: undefined as unknown as T };
  try {
    return { ok: true, value: JSON.parse(text) as T };
  } catch {
    return { ok: false, error: 'The device sent a reply that was not JSON' };
  }
}

export function getStatus(opts: DeviceOptions = {}): Promise<JsonResult<DeviceStatus>> {
  return jsonCall<DeviceStatus>('/api/status', { method: 'GET', cache: 'no-store' }, opts);
}

export function getConfig(opts: DeviceOptions = {}): Promise<JsonResult<unknown>> {
  return jsonCall<unknown>('/api/config', { method: 'GET', cache: 'no-store' }, opts);
}

export async function putConfig(
  doc: unknown,
  opts: DeviceOptions = {},
): Promise<JsonResult<{ status?: string; restarting?: boolean; warning?: string }>> {
  return jsonCall('/api/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(doc),
  }, opts);
}

export function requestRefresh(opts: DeviceOptions = {}): Promise<JsonResult<unknown>> {
  return jsonCall('/api/refresh', { method: 'POST' }, opts);
}

/**
 * Ask the device to draw a SPECIFIC page now (FR-15).
 *
 * WHY THIS IS NEEDED: the device chooses its page from its own rotation schedule, so a page the
 * user is editing may be up to a full rotation (fifteen minutes with the shipped intervals) away
 * from being shown — and a value box they just moved onto it would look ignored. This makes the
 * device show the edited page at once, which is also what lets the editor's value preview fill in
 * without waiting for the scheduler.
 *
 * It implies a full refresh on the device: a different page is a different background, which the
 * partial path cannot diff against.
 */
export function requestPage(page: number, opts: DeviceOptions = {}): Promise<JsonResult<unknown>> {
  return jsonCall(`/api/refresh?page=${encodeURIComponent(String(page))}`, { method: 'POST' }, opts);
}

/** One widget's resolved value, as the device last drew it (FR-27). */
export interface DeviceValue {
  id: string;
  text: string;
  has_value: boolean;
  /**
   * THE RAW READING BEHIND `text`, and whether `text` really is its plain rendering.
   *
   * Without these the editor can only echo `text`, which the device formatted with the STORED
   * decimals/prefix/suffix — so editing one of those would leave the box showing the old formatting
   * until the config was saved and the panel had redrawn. That is the reported "the layout editor
   * doesn't show the updated values unless you first save and refresh".
   *
   * `rendered_number` GATES THE RE-FORMAT, and it is reported rather than inferred because it cannot
   * be inferred: a firing alert replaces the reading with a level word, a text reading carries a
   * condition or a sensor state, an icon binding carries the OWM code, and a failed fetch shows the
   * widget's fallback — and a fallback of "72.5" is indistinguishable from a reading of 72.5. The
   * device knows which branch it took, so it says so, and everything that is not `rendered_number: 1`
   * is echoed verbatim (which is always correct, since `text` is what the panel drew).
   *
   * A NUMBER, NOT A BOOLEAN: the firmware serialises it as `1`/`0`, so a `=== true` test silently
   * never matched — the raw reading was fetched, dropped, and the preview went on echoing the old
   * formatting, which is the very symptom this field exists to fix. Test it for truthiness.
   */
  rendered_number?: number;
  value?: number;
}

export interface DeviceValues {
  /** The page these values are for — the one that was asked about, not necessarily the drawn one. */
  page: number;
  /** The page currently drawn on the panel, or -1 while nothing has been drawn. */
  drawn_page?: number;
  page_count: number;
  resolved_at: number;
  values: DeviceValue[];
}

/** A reading the device resolved, as the editor needs it: the exact string it will draw plus,
 *  when that string is a number's plain rendering, the number itself so the app can re-format it. */
export interface LiveValue {
  text: string;
  /** Present only when the device reported `rendered_number`; `value` is then the raw reading. */
  value?: number;
}

/**
 * The values the device resolved for `page` (default: the page on the glass), keyed by widget id.
 *
 * THIS IS WHAT MAKES THE PREVIEW HONEST (FR-27: "rendered with real fetched data"). The device
 * holds the OWM key and the HA token and has already run every widget through the same formatter
 * that draws the string on the glass, so asking it is the only way the preview can agree with the
 * panel without a second implementation of the resolution rules — and in the embedded case the
 * browser has no credentials and, on the setup network, no internet at all.
 *
 * ASK FOR THE PAGE BEING EDITED. The device rotates pages on its own, so a caller that did not
 * name a page got whatever was on the glass — every other page's boxes read "--" and the user
 * concluded the fetch was broken. The device now resolves every page, so `page` selects one; when
 * omitted it answers for the drawn page, which is what the "which page is the display showing"
 * readout wants. `drawnPage` is reported separately so that readout keeps working while the editor
 * previews a page the glass is not on.
 *
 * Returns null when the device is unreachable, which is distinct from "this page has no values". */
export async function getValuesInfo(
  opts: DeviceOptions = {},
  page?: number,
): Promise<{ page: number; drawnPage: number; pageCount: number; values: Record<string, LiveValue> } | null> {
  const q = page === undefined ? '' : `?page=${encodeURIComponent(String(page))}`;
  const res = await jsonCall<DeviceValues>(`/api/values${q}`, { method: 'GET', cache: 'no-store' }, opts);
  if (!res.ok) return null;
  const values: Record<string, LiveValue> = {};
  for (const v of res.value.values ?? []) {
    if (typeof v.id === 'string' && typeof v.text === 'string') {
      values[v.id] = { text: v.text };
      /* Carry the number with it ONLY when the device says the text is that number's rendering. A
       * numeric value without the flag would have the editor re-format an alert word or a sensor
       * state into a number the panel never shows. The flag arrives as a NUMBER (the firmware
       * writes 1/0), so it is tested for truthiness rather than against `true`. */
      if (v.rendered_number && typeof v.value === 'number' && Number.isFinite(v.value)) {
        values[v.id] = { text: v.text, value: v.value };
      }
    }
  }
  return {
    page: res.value.page ?? 0,
    drawnPage: res.value.drawn_page ?? res.value.page ?? 0,
    pageCount: res.value.page_count ?? 1,
    values,
  };
}

export interface AuthState {
  enabled: boolean;
  wantEnabled: boolean;
  token: string;
}

export function getAuth(opts: DeviceOptions = {}): Promise<JsonResult<AuthState>> {
  return jsonCall<AuthState>('/api/auth', { method: 'GET', cache: 'no-store' }, opts);
}

export function putAuth(
  body: { enabled?: boolean; token?: string },
  opts: DeviceOptions = {},
): Promise<JsonResult<AuthState>> {
  return jsonCall<AuthState>('/api/auth', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }, opts);
}

/** Which credentials the device has stored. Values are NOT included — see the device's
 *  /api/secrets — except the HA URL, which the app needs to offer the entity picker. */
export interface SecretsState {
  owmKey: boolean;
  haToken: boolean;
  haUrl: string;
}

export function getSecrets(opts: DeviceOptions = {}): Promise<JsonResult<SecretsState>> {
  return jsonCall<SecretsState>('/api/secrets', { method: 'GET', cache: 'no-store' }, opts);
}

/**
 * Store credentials on the device.
 *
 * AN ABSENT OR EMPTY FIELD LEAVES THE STORED VALUE ALONE, which is why the caller sends only
 * the fields the user actually filled in. The device applies the same rule, so a form submit
 * with a blank OWM key cannot wipe the working one.
 */
export function putSecrets(
  body: { owmKey?: string; haUrl?: string; haToken?: string },
  opts: DeviceOptions = {},
): Promise<JsonResult<{ status?: string }>> {
  return jsonCall('/api/secrets', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  }, opts);
}
