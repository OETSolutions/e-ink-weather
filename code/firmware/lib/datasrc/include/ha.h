#pragma once
#include "datasrc.h"

/* Home Assistant REST parsing (FR-5a, FR-5b).
 *
 * WHY THIS SHAPE: HA's POST /api/template renders a template server-side and returns
 * plain text, so the device sends ONE tiny request for exactly the entities the current
 * layout needs and gets back a small '|'-separated line. That beats N per-entity GETs,
 * and it beats pulling /api/states (which returns every entity on the instance). */

/* Parse one '|'-separated line, e.g. "68.4|41.2|unavailable", into values in order.
 * Returns how many tokens were parsed (may be < n_out on short input); every slot not
 * filled by the response is set to DATASRC_ERR_UNAVAILABLE, NEVER to zero — a missing
 * entity and a genuine 0 degF reading must not look the same on the glass. */
int ha_parse_template_line(const char *line, double *out,
                           datasrc_status_t *status, int n_out);

/* Classify a single HA state string. HA states are STRINGS and are routinely
 * "unavailable" or "unknown" (a sleeping sensor, a dead Zigbee node), so they must never
 * be cast blindly (FR-5b). Surrounding whitespace is ignored — an HTTP body can arrive
 * with a trailing newline, and that must not turn the last entity into UNAVAILABLE. */
datasrc_status_t ha_classify_state(const char *state, double *out_value);

/* Validate an entity_id against HA's grammar (lowercase alphanumerics and underscores,
 * at least one '.', non-empty domain and object_id). Exported because the MQTT topic
 * builder (ha_mqtt.h) must enforce the SAME rule — two copies of a grammar drift, and
 * the looser copy becomes the hole. */
int ha_entity_id_valid(const char *entity_id);

/* Append one entity to the template being built (the '|' separator is inserted
 * automatically after the first). Start with buf[0]='\0' and len=0.
 * Returns the new length, or -1 if it would not fit.
 *
 * `entity_id` is validated against HA's real entity-id grammar (lowercase alphanumerics
 * and underscores, at least one '.') and rejected otherwise. This is a trust boundary:
 * the id can come from the config UI, and an unvalidated id would be interpolated into
 * a Jinja template — `states('...')` with an embedded quote would be template injection,
 * not merely a bad request. */
int ha_template_add_entity(char *buf, int buflen, int len, const char *entity_id);
