#pragma once

/* Home Assistant MQTT statestream topic handling (FR-5c).
 *
 * WHY THIS SHAPE: the REST path (ha.h) makes the device ASK for a value, so the device
 * polls on its own schedule and burns a TLS handshake plus a round trip per refresh.
 * With `mqtt_statestream` HA publishes every state change to
 *     <base_topic>/<domain>/<object_id>/state
 * and the device just listens. A sleeping sensor and a dead network then look different:
 * a value that stops arriving is stale, whereas REST cannot distinguish "unchanged" from
 * "unreachable" without re-asking. The trade is that the broker must be reachable at the
 * moment of the change, so the last retained value is the fallback. */

/* Build the statestream topic for `entity_id` into `buf`.
 * Returns the topic length, or -1 if the entity is invalid or it would not fit.
 *
 * The entity id is checked with ha_entity_id_valid() — the SAME validator the REST
 * template builder uses. The id comes from the config UI, and an unvalidated id would
 * be pasted straight into a topic string: an embedded '/' would silently retarget the
 * subscription to a different entity, and a '#' or '+' would turn it into a wildcard
 * that matches entities the layout never asked for. */
int ha_mqtt_state_topic(char *buf, int buflen, const char *base_topic,
                        const char *entity_id);

/* Invert ha_mqtt_state_topic(): given a received topic, recover "domain.object_id".
 * Returns the entity-id length, or -1 if `topic` is not under `base_topic` or does not
 * have exactly the statestream shape.
 *
 * Shape is enforced exactly: base + '/' + domain + '/' + object_id + "/state". A topic
 * with a trailing segment (".../state/extra") is REJECTED rather than parsed as if it
 * ended at "/state" — otherwise a neighbouring statestream attribute topic would be
 * mistaken for the state itself. */
int ha_mqtt_entity_from_topic(const char *topic, const char *base_topic,
                              char *out, int outlen);

/* Undo the JSON string encoding HA applies to state payloads: statestream publishes a
 * string state as a quoted, escaped JSON string, so `on` arrives as `"on"`.
 * Returns the unquoted length, or -1 if the payload does not fit in `out`.
 *
 * Truncation is an ERROR here, not a silent clip. A half-copied state would be compared
 * against the layout and could match the wrong entity's value; the caller must be able
 * to tell that the payload was too big and drop the message instead. */
int ha_mqtt_unquote(const char *payload, char *out, int outlen);
