#pragma once

/* The NVS keys the device reads and writes, in one place.
 *
 * WHY THIS EXISTS: the credential path was broken precisely because two files each spelled
 * the keys out as literals. Provisioning stored the SSID under one name while the refresh
 * path read it from another, so a successfully provisioned device looked unconfigured and
 * re-entered provisioning on every boot. Nothing failed loudly — the device just quietly
 * never worked. A shared header makes that class of bug a compile error.
 *
 * The VALUES must not change. They name keys already written on provisioned devices, so
 * editing one silently strands every deployed unit: it comes up unconfigured and the old
 * value sits in flash forever. Renames need a migration, not an edit. */

#define DEVENV_NVS_NAMESPACE "devcfg"

/* WiFi credentials, written by provisioning (FR-30) and read by app_refresh.c.
 *
 * These are ALSO mirrored into the WiFi driver's own namespace (nvs.net80211) — see
 * prov_store_credentials() for why the copy is unavoidable rather than redundant. */
#define DEVENV_KEY_WIFI_SSID "wifi_ssid"
#define DEVENV_KEY_WIFI_PASS "wifi_pass"

/* Secrets entered through the config UI (FR-30). Never compiled in and never committed. */
#define DEVENV_KEY_OWM_KEY  "owm_key"
#define DEVENV_KEY_HA_URL   "ha_url"
#define DEVENV_KEY_HA_TOKEN "ha_token"

/* Location, stored as raw doubles (blob) rather than strings: the value is consumed as a
 * number to build the OWM query, and a string would need parsing at every fetch with a
 * locale-dependent result. */
#define DEVENV_KEY_LOC_LAT "loc_lat"
#define DEVENV_KEY_LOC_LON "loc_lon"

/* Optional API authentication (FR-31). Two keys rather than one, because "the owner asked
 * for auth" and "there is a token to check" are different facts: an enabled flag with no
 * token would demand a credential that does not exist, so apiauth_required() requires both.
 * Storing the token as a string keeps it readable by the config app, which has to show the
 * user the token they must type into the client. */
#define DEVENV_KEY_API_AUTH_ENABLED "api_auth_on"
#define DEVENV_KEY_API_TOKEN        "api_token"
