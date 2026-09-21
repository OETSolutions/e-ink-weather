#pragma once

#include <stddef.h>

/* Builds the `{"list":[{"ssid":"...","rssi":-42},...]}` document the provisioning network
 * picker consumes.
 *
 * WHY THIS IS A LIBRARY AND NOT CODE IN prov_ap.c. It was in prov_ap.c and it had a heap
 * overflow. The bug was not exotic: the buffer was 2 KB and the writer appended with
 * `o += snprintf(out + o, 2048 - o, ...)`. That is correct only while `o` stays under 2048.
 * snprintf returns the length it WOULD have written, so an entry that does not fit still
 * advances `o` past the end — and then `2048 - o` is a negative int converted to size_t, i.e.
 * a near-SIZE_MAX count, so the next entry was told it had the whole address space to write in
 * and scribbled off the end of the heap block. One long SSID made it worse: json_escape expands
 * every byte to `\u00xx`, so a 32-byte name is 192 bytes on the wire and a full picker needs
 * ~5 KB — the overflow was reachable with an ordinary list of non-Latin network names.
 *
 * The reason it now lives here is that an ESP-IDF component cannot be host-tested: the bug was
 * invisible to the test suite for exactly that reason, and a green build said nothing about it.
 * In lib/ it is driven by real inputs under the native env, canaries included, so "the buffer
 * cannot be overrun" is a property the suite checks rather than a comment someone wrote.
 *
 * USAGE. `o` is an offset owned by the caller, starting at 0:
 *
 *     size_t o = 0;
 *     if (provjson_list_begin(buf, cap, &o) != 0) { ... }
 *     for (...) {
 *         if (provjson_list_add(buf, cap, &o, ssid, rssi, first) != 0) break;
 *         first = 0;
 *     }
 *     provjson_list_end(buf, cap, &o);
 *
 * Every function leaves `buf` NUL-terminated and never writes outside [buf, buf + cap). */

/* The longest SSID the Wi-Fi stack reports, and the most bytes json_escape can turn it into
 * (every byte expanded to `\u00xx`). PROVJSON_ENTRY_MAX is the worst case for one entry,
 * INCLUDING its separating comma, and is what the room check is sized against. */
#define PROVJSON_MAX_SSID_BYTES 32
#define PROVJSON_MAX_ESCAPED    (PROVJSON_MAX_SSID_BYTES * 6)
#define PROVJSON_ENTRY_MAX      (1 + 9 + PROVJSON_MAX_ESCAPED + 14)

/* Escape `s` into JSON string-literal form.
 *
 * `"` and `\` get a backslash; every other byte below 0x20, and every byte >= 0x80, becomes
 * `\u00xx`. Escaping high bytes rather than passing them through is what keeps the output
 * valid JSON whatever the network is called: the picker receives bytes off the air, not a
 * validated string, and a raw invalid UTF-8 byte would make the whole document unparseable —
 * one badly-named AP taking out every other network in the list.
 *
 * `out` is always NUL-terminated and never overrun; a name too long to escape in full is
 * TRUNCATED at the last complete escape. Returns 0, or -1 if `out` is NULL or `out_max` is 0. */
int provjson_escape(const char *s, char *out, size_t out_max);

/* Open the list. Returns 0, or -1 if the buffer cannot hold even an empty list. */
int provjson_list_begin(char *buf, size_t cap, size_t *o);

/* Append one network. `first` suppresses the separating comma on the first entry.
 *
 * Returns 0 if the entry was appended; 1 if it did NOT fit, in which case nothing was written,
 * `o` is unchanged, and the caller should stop adding (the document is still valid and
 * complete up to this point); -1 on bad arguments. The room check is against the WORST CASE
 * entry size and reserves two more bytes for the closing `]}`, so a partial write is never the
 * way the buffer is kept safe. */
int provjson_list_add(char *buf, size_t cap, size_t *o, const char *ssid, int rssi, int first);

/* Close the list. A no-entry list still produces the valid document `{"list":[]}`. */
int provjson_list_end(char *buf, size_t cap, size_t *o);
