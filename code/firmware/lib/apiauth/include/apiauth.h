#pragma once

#include <stddef.h>

/* Optional bearer-token authentication for the device API (FR-31).
 *
 * WHY THIS IS OPTIONAL AND OFF BY DEFAULT: the device is designed for a trusted home
 * network, and FR-31 makes the config app same-origin with the API — so a token would
 * otherwise have to be entered before the device could be configured at all, which is a
 * worse first-run experience than the thing it protects against. The user asked for a
 * checkbox, so this is a policy the owner opts into, not a default.
 *
 * WHY IT STILL MATTERS: POST /api/ota takes a CLIENT-SUPPLIED url and installs firmware from
 * it. On a trusted LAN that is a convenience; from anywhere else it is a remote
 * firmware-replacement endpoint with no password. This is the one control that stands between
 * "someone on my wifi" and "someone owns my device", and it is the reason the feature exists
 * even though the default is off.
 *
 * The wire format and the decision logic are here, in a library, because they are pure and
 * easy to get subtly wrong — a token compare that leaks length by returning early, or an
 * "enabled with no token set" state that locks the owner out of their own device. The NVS
 * reads and the HTTP plumbing stay in the api component. */

/* The longest accepted token, in characters (excluding the terminator). A long-lived HA token
 * is ~180 chars; 128 is generous for a device-generated one and bounds the NVS value and the
 * comparison. A token of exactly this length is usable; one longer is refused. */
#define APIAUTH_TOKEN_MAX 128

/* Compare an `Authorization` header against the expected token.
 *
 * Accepts exactly `Bearer <token>` (scheme case-insensitive, exactly one space). Returns 1 on
 * a match, 0 otherwise — including a missing header, a wrong scheme, or a wrong token.
 *
 * The comparison is LENGTH-INDEPENDENT and does not return early on the first differing
 * byte. That is not paranoia for its own sake: an early-return compare leaks the token's
 * length and prefix through response timing, and a token is the one secret where "almost
 * right" is worth attacking. A `NULL` or empty `expected` never matches, so a device in the
 * "auth off" state cannot be unlocked by sending an empty bearer. */
int apiauth_header_matches(const char *authorization, const char *expected);

/* Should a mutating request be required to authenticate?
 *
 * Only when the owner enabled it AND a token is actually set. This is the guard that makes
 * the toggle safe: an "enabled" flag with no token would demand a credential that does not
 * exist, bricking the config app with no way back except a factory reset. Requiring nothing
 * in that state is the recoverable behaviour. */
int apiauth_required(int enabled, const char *token);

/* Is `token` acceptable to store? Returns 1 if it is usable as a credential.
 *
 * Rejects empty, over-long, and anything containing a control byte or a space — the header
 * parser splits on the first space, so a token with a space inside could never be sent
 * successfully, and storing it would silently enable a lock whose key cannot be turned. It
 * is better to refuse the token at the moment it is set, where the user can see the error,
 * than to accept it and fail every later request. */
int apiauth_token_is_usable(const char *token);

/* A token the device can generate for the user, so they never have to invent one. Fills
 * `out` with `len` random hex characters and NUL-terminates; `len` must be even and
 * `out` must hold len+1 bytes. The caller supplies the entropy (rand()) so this stays pure
 * and host-testable. */
void apiauth_make_token(char *out, size_t len, unsigned (*rand_next)(void));
