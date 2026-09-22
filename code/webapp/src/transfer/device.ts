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

/** One widget's resolved value, as the device last drew it (FR-27). */
export interface DeviceValue {
  id: string;
  text: string;
  has_value: boolean;
}

export interface DeviceValues {
  page: number;
  page_count: number;
  resolved_at: number;
  values: DeviceValue[];
}

/**
 * The values the device last resolved, keyed by widget id.
 *
 * THIS IS WHAT MAKES THE PREVIEW HONEST (FR-27: "rendered with real fetched data"). The device
 * holds the OWM key and the HA token and has already run every widget through the same formatter
 * that draws the string on the glass, so asking it is the only way the preview can agree with the
 * panel without a second implementation of the resolution rules — and in the embedded case the
 * browser has no credentials and, on the setup network, no internet at all.
 *
 * Returns an empty map when the device has not refreshed yet or is unreachable; the caller falls
 * back to its own placeholder rendering, which is what the panel would show before its first
 * fetch anyway.
 */
export async function getValues(opts: DeviceOptions = {}): Promise<Record<string, string>> {
  const res = await jsonCall<DeviceValues>('/api/values', { method: 'GET', cache: 'no-store' }, opts);
  if (!res.ok) return {};
  const out: Record<string, string> = {};
  for (const v of res.value.values ?? []) {
    if (typeof v.id === 'string' && typeof v.text === 'string') out[v.id] = v.text;
  }
  return out;
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
