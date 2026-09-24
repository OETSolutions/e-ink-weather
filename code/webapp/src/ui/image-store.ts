/**
 * Where an uploaded picture lives while it is being used, and after a reload.
 *
 * WHY THIS EXISTS AT ALL: the picture's PIXELS never enter the config (see ImageBinding in
 * model/config.ts — the device re-parses the config every refresh tick and a PUT already fails
 * above ~12 KB). They are baked into the page's static artwork instead. But the editor still has
 * to be able to RE-DITHER the picture when the user resizes the box, and it must keep doing so
 * after every repaint — which rebuilds the layer from scratch.
 *
 * TWO TIERS, ON PURPOSE:
 *
 *   - An in-memory Map is the source of truth at RENDER time. pageImages() runs inside the layer
 *     builder on every repaint, so a lookup has to be synchronous — an async IndexedDB read there
 *     would either block the repaint or make the layer builder async, and both are worse than
 *     holding a decoded bitmap for the session.
 *
 *   - IndexedDB persists the ORIGINAL FILE BYTES across reloads. Without it the pictures would
 *     vanish on every refresh of the page, and since a Save re-bakes the WHOLE artwork set, a
 *     reload would mean either losing the pictures off the display or refusing to save at all.
 *     IndexedDB rather than localStorage because these are binary blobs of megabytes and
 *     localStorage is a string-only ~5 MB store.
 *
 * It stores the file's BYTES rather than the decoded bitmap, because an ImageBitmap cannot be
 * serialised — and re-decoding on load is cheap and happens once.
 */

export const IMAGE_DB = 'eink-weather-images';
const STORE = 'images';
const DB_VERSION = 1;

const images = new Map<string, ImageBitmap>();

/* The keys whose bytes are in IndexedDB. Tracked so a save knows whether a picture would be LOST,
 * which is a different question from whether one is held in memory right now: after a reload the
 * bytes are there and the decode has not run yet. */
const persisted = new Set<string>();

/** The source picture for a widget, or undefined when this browser has none. SYNCHRONOUS: the
 *  render path calls this. Call loadImagesFromDb() once at startup to fill it. */
export function getImage(widgetId: string): ImageBitmap | undefined {
  return images.get(widgetId);
}

/** Remember the source picture for a widget, releasing whatever it replaced. */
export function setImage(widgetId: string, img: ImageBitmap): void {
  const old = images.get(widgetId);
  /* Freed explicitly rather than left to GC: an ImageBitmap holds decoded pixel memory outside the
   * JS heap, so replacing one on every resize of a large photo would accumulate until the tab was
   * collected. `close()` is the documented way to release it. */
  if (old && old !== img) old.close();
  images.set(widgetId, img);
}

/** Forget a widget's picture, releasing it and dropping the persisted copy. Called when its box is
 *  deleted, so a removal does not come back after a reload. */
export function clearImage(widgetId: string): void {
  const old = images.get(widgetId);
  if (old) old.close();
  images.delete(widgetId);
  persisted.delete(widgetId);
  void deleteBytes(widgetId);
}

/** Whether this browser can put a picture in the widget's box on the next Save.
 *
 *  TRUE FOR A PICTURE STILL IN INDEXEDDB BUT NOT YET DECODED, which is the state right after a
 *  reload: the bytes are there, so a Save will not lose the picture. Asking the in-memory map alone
 *  would make the editor warn about pictures it can perfectly well restore. */
export function hasImage(widgetId: string): boolean {
  return images.has(widgetId) || persisted.has(widgetId);
}

/** Whether a binding describes an image box. */
export function isImageBinding(b: { kind?: string } | undefined): b is { kind: 'image' } {
  return !!b && b.kind === 'image';
}

/** Release every held picture. Called on unload so a page that is navigated away does not leak the
 *  decoded bitmaps until the process ends. The PERSISTED copies stay. */
export function releaseAllImages(): void {
  for (const img of images.values()) img.close();
  images.clear();
}

/* ---------------------------------------------------------------- IndexedDB ---- */

function openDb(): Promise<IDBDatabase | null> {
  return new Promise((resolve) => {
    /* A browser with IndexedDB disabled (private mode in some engines) must not break the editor:
     * resolve null and let the caller carry on in-memory. The pictures simply will not survive a
     * reload, which the panel already reports via hasImage(). */
    if (typeof indexedDB === 'undefined') { resolve(null); return; }
    let req: IDBOpenDBRequest;
    try {
      req = indexedDB.open(IMAGE_DB, DB_VERSION);
    } catch {
      resolve(null);
      return;
    }
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => resolve(null);
  });
}

/** Persist a picture's original bytes for a widget. Best-effort: a failure means the picture will
 *  not survive a reload, not that the upload failed — so it is reported and swallowed. */
export async function persistImage(widgetId: string, file: File): Promise<void> {
  persisted.add(widgetId);
  const db = await openDb();
  if (!db) return;
  const bytes = await file.arrayBuffer();
  await new Promise<void>((resolve) => {
    const tx = db.transaction(STORE, 'readwrite');
    tx.objectStore(STORE).put(bytes, widgetId);
    tx.oncomplete = () => resolve();
    tx.onerror = () => resolve();
    tx.onabort = () => resolve();
  });
  db.close();
}

async function deleteBytes(widgetId: string): Promise<void> {
  const db = await openDb();
  if (!db) return;
  await new Promise<void>((resolve) => {
    const tx = db.transaction(STORE, 'readwrite');
    tx.objectStore(STORE).delete(widgetId);
    tx.oncomplete = () => resolve();
    tx.onerror = () => resolve();
    tx.onabort = () => resolve();
  });
  db.close();
}

/**
 * Load every persisted picture into memory, decoding each.
 *
 * CALLED ONCE AT STARTUP, before the first layer is built — otherwise a reloaded page would render
 * its image boxes blank and then fill them in a moment later, and a Save in that window would bake
 * the blank version.
 *
 * A picture whose bytes no longer decode (an unsupported format, or a partial write) is DROPPED
 * rather than kept as a permanent error: it is reported to the caller, and the box then behaves as
 * one whose picture this browser does not have — which is the honest state, and one the panel
 * already explains.
 *
 * Returns the ids that could not be decoded.
 */
export async function loadImagesFromDb(): Promise<string[]> {
  const db = await openDb();
  if (!db) return [];
  const ids = await new Promise<string[]>((resolve) => {
    const tx = db.transaction(STORE, 'readonly');
    const req = tx.objectStore(STORE).getAllKeys();
    req.onsuccess = () => resolve((req.result as IDBValidKey[]).map(String));
    req.onerror = () => resolve([]);
  });
  const failed: string[] = [];
  for (const id of ids) {
    const bytes = await new Promise<ArrayBuffer | undefined>((resolve) => {
      const tx = db.transaction(STORE, 'readonly');
      const req = tx.objectStore(STORE).get(id);
      req.onsuccess = () => resolve(req.result as ArrayBuffer | undefined);
      req.onerror = () => resolve(undefined);
    });
    if (!bytes) continue;
    persisted.add(id);
    try {
      const img = await createImageBitmap(new Blob([bytes]));
      images.set(id, img);
    } catch {
      failed.push(id);
    }
  }
  db.close();
  return failed;
}
