#pragma once

/* Parsing for the public-IP geolocation service (ip-api.com), used to pre-fill the device's
 * location fields with a city-level approximation (FR-30).
 *
 * WHY THIS IS A LIBRARY AND NOT CODE IN THE net COMPONENT: it is a pure string function with
 * one interesting failure mode — the service answers HTTP 200 for a REJECTED query, with
 * {"status":"fail"} and no coordinates — and that failure mode is exactly what a host test can
 * pin down. The network call and the NVS write stay in the component, where they cannot be
 * tested without a device; the part that can be wrong in a subtle way is here, where it is
 * covered. The same split as owm.c and provdns.c. */

/* Parse a lat/lon (and optionally a city) out of an ip-api.com /json/ reply.
 *
 * Returns 0 and fills the two coordinates only for a definite success. Returns -1 without
 * touching the outputs for anything else: a "fail" status, a missing or non-numeric
 * coordinate, or a malformed document. The caller must treat -1 as "leave the location
 * alone", never as 0,0.
 *
 * `city` may be NULL. When given it is filled with the place name, or set empty if the reply
 * had none. `city_len` must cover the terminator. */
int geoloc_parse(const char *json, double *lat, double *lon, char *city, unsigned city_len);
