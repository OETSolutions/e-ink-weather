/* Approximate location from the public IP. See geo_ip.c for why this exists and what it is
 * (and is not) good for. */
#pragma once

#include "esp_err.h"
#include <stddef.h>

/* Look up an approximate lat/lon for this device's public IP. `city` may be NULL. Returns
 * ESP_OK only on a definite answer; anything else leaves the outputs untouched. */
esp_err_t geo_ip_lookup(double *lat, double *lon, char *city, size_t city_len);

/* Fill DEVENV_KEY_LOC_LAT/LON from the IP **only when no location is stored yet**. Never
 * overwrites a user's coordinates. Returns 1 if it wrote a new location, else 0. Safe to call
 * whenever the network is up; it is a no-op on a configured device, so it costs one NVS read. */
int geo_ip_fill_if_unset(void);
