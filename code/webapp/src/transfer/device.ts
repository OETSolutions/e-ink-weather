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
