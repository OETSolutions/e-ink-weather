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
 * with a trailing newline, and that must not turn the last entity into UNAVAILABLE.
 *
 * THIS IS THE NUMERIC-ONLY VIEW: it returns OK only for a plain finite number and
 * UNAVAILABLE for everything else, including a perfectly good text state. Callers that
 * must DRAW a non-numeric state (a binary_sensor's "on"/"off") want
 * ha_classify_state_text() — see its note for why the two are different questions. */
datasrc_status_t ha_classify_state(const char *state, double *out_value);

/* Classify a state preserving its TEXT, for widgets that draw a word rather than a number.
 *
 * WHY THIS EXISTS: a binary_sensor reports "on"/"off", a lock "locked", a climate "heat" — real
 * readings that are not numbers. The numeric classifier rejected all of them as UNAVAILABLE, so
 * the widget printed its fallback and the user saw "--" for a sensor that was working. The two
 * questions are different: "is this a number I can compare" and "is this a real reading I can
 * show". `out->is_numeric` distinguishes them for the caller.
 *
 * "unavailable"/"unknown" remain UNAVAILABLE with NO text: a truly missing reading must fall back
 * rather than print the word "unavailable" as though it were the value. */
datasrc_status_t ha_classify_state_text(const char *state, datasrc_value_t *out);

/* The text-preserving parse of one '|'-separated line, parallel to ha_parse_template_line(). */
int ha_parse_template_line_text(const char *line, datasrc_value_t *out, int n_out);

/* Validate an entity_id against HA's grammar (lowercase alphanumerics and underscores,
 * at least one '.', non-empty domain and object_id). Exported because the MQTT topic
 * builder (ha_mqtt.h) must enforce the SAME rule — two copies of a grammar drift, and
 * the looser copy becomes the hole. */
int ha_entity_id_valid(const char *entity_id);

/* Validate an entity-id SEARCH substring, for the config UI's entity picker.
 *
 * THE SEARCH IS A TRUST BOUNDARY: the device runs it as a server-side Jinja template and
 * interpolates the substring into the template text, so a single quote or a brace in the query
 * would not be a bad request — it would be template injection. Requiring the entity-id
 * character set (lowercase, digits, '_', '.') both admits every substring a real entity id can
 * contain and leaves nothing Jinja or the JSON body meaningfully interprets. Capped well below
 * any real id length. Returns 1 for a usable query. */
int ha_search_query_valid(const char *q);

/* One entity row from the picker's search response.
 *
 * THE ID BUFFER MUST FIT REAL IDS. It was 64, and a real automation on the bench —
 * "automation.turn_on_family_room_vent_fan_when_upstairs_hallway_cooling" at 69 chars — was
 * dropped by the length guard and never appeared in the picker, while the same response's total
 * said there were more matches than rows. HA ids are bounded only by the object_id, so 128 leaves
 * room for any id a real instance carries. THE NAME IS SHORTER ON PURPOSE and is sliced by the
 * template: it is display text, and capping it server-side keeps the response bounded whatever a
 * friendly_name contains. */
#define HA_ENTITY_ID_LEN   128
#define HA_ENTITY_NAME_LEN 64

typedef struct {
    char id[HA_ENTITY_ID_LEN];
    char name[HA_ENTITY_NAME_LEN];
} ha_entity_t;

/* Parse the picker's 'id|name' newline-separated template output into rows.
 *
 * WHY THE TEMPLATE RETURNS THIS SHAPE: the device asks HA to render the search server-side
 * (one small request, the same reason fetch_ha() uses /api/template), so the reply is plain
 * text rather than JSON. `out` receives at most `max` rows; `total` receives how many valid rows
 * the template produced, so the caller can distinguish "nothing matched" from "the match list
 * was clipped" instead of silently reporting a short list as complete. Returns the number of
 * rows written (<= max). */
int ha_parse_entity_list(const char *body, ha_entity_t *out, int max, int *total);

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
