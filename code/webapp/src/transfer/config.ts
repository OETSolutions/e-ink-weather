/**
 * Config export and import (FR-26, FR-26a, FR-26b).
 *
 * A config is saved as a versioned JSON file the user downloads and uploads. NOT the File
 * System Access API: MDN classes showSaveFilePicker()/showOpenFilePicker() as "Limited
 * availability … not Baseline … Experimental", requiring a secure context — and the device is
 * served over plain HTTP on a LAN IP, which rules it out as the primary mechanism. A download
 * and a file input work everywhere, including the exact browsers this device will meet.
 */

import { SCHEMA_VERSION, emptyConfig, type Config } from '../model/config';

/**
 * Serialise a config for saving.
 *
 * Written PRETTY (two-space indent) on purpose: this file is something a person may open,
 * diff, or hand-edit to fix one coordinate, and a single minified line makes that impossible.
 * The cost is bytes in a text file on a laptop, which is the cheapest thing here.
 */
export function exportConfig(c: Config): string {
  return JSON.stringify(c, null, 2);
}

/**
 * Parse a saved config file.
 *
 * A NEWER SCHEMA VERSION IS REFUSED, not read on a best-effort basis (FR-26b). The same field
 * name may mean something different in the next version, and guessing would silently apply a
 * layout the firmware misreads — a wrong picture on the glass with no error anywhere. Refusing
 * gives the user a message they can act on.
 *
 * An OLDER version is accepted: the document is merged over the current defaults, so a file
 * saved before a field existed still loads with a sensible value for it.
 */
export function importConfig(text: string): Config {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error('That file is not valid JSON.');
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    throw new Error('That file does not contain a layout.');
  }

  const doc = parsed as Partial<Config>;
  const v = doc.schemaVersion;
  if (typeof v !== 'number' || !Number.isFinite(v)) {
    /* A document without a version cannot be migrated, so it is refused rather than assumed to
     * be current — the same rule cfg_store_put() applies on the device. */
    throw new Error('That file has no schema version, so it cannot be read safely.');
  }
  if (v > SCHEMA_VERSION) {
    throw new Error(
      `That file was saved by a newer version of the app (schema ${v}; this one understands ` +
      `${SCHEMA_VERSION}). Update the app before loading it.`,
    );
  }

  /* Merge over the defaults, treating an ABSENT field as absent: a spread of `doc` would copy
   * explicit `undefined`s over the baseline and break readers of those fields. */
  const base = emptyConfig();
  const merged: Config = { ...base, ...doc } as Config;
  if (!Array.isArray(doc.pages) || doc.pages.length === 0) merged.pages = base.pages;
  merged.location = { ...base.location, ...(doc.location ?? {}) };
  merged.ha = { ...base.ha, ...(doc.ha ?? {}) };
  for (const p of merged.pages) {
    if (!Array.isArray(p.widgets)) p.widgets = [];
  }
  return merged;
}

/** A filename for a downloaded config, with a date so several saves do not collide. */
export function configFilename(now: Date = new Date()): string {
  const p = (n: number) => String(n).padStart(2, '0');
  return `eink-weather-${now.getFullYear()}-${p(now.getMonth() + 1)}-${p(now.getDate())}.json`;
}

/**
 * Trigger a download of `text` as `filename`.
 *
 * The object URL is REVOKED after the click. Without that the blob stays alive for the life of
 * the document, so a user who saves a few layouts in one session leaks each one — small here,
 * but it is the kind of thing that looks like a mystery later.
 */
export function downloadText(text: string, filename: string): void {
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.append(a);
  a.click();
  a.remove();
  /* Revoke on the next tick: revoking synchronously can cancel the download in some browsers
   * before it has read the blob. */
  setTimeout(() => URL.revokeObjectURL(url), 0);
}
