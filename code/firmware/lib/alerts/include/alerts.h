#pragma once

typedef enum { ALERT_NONE = 0, ALERT_ADVISORY, ALERT_WARNING, ALERT_SEVERE } alert_level_t;

typedef enum { ALERT_OP_GT = 0, ALERT_OP_GTE, ALERT_OP_LT, ALERT_OP_LTE,
               ALERT_OP_EQ, ALERT_OP_NE } alert_op_t;

typedef struct {
    alert_op_t    op;
    double        threshold;
    alert_level_t level;
} alert_rule_t;

/* Returns the rule's level when the comparison holds, else ALERT_NONE.
 * A non-finite value (NaN/inf, e.g. a sensor reporting "unavailable")
 * always returns ALERT_NONE — a missing reading must never raise an alarm. */
alert_level_t alerts_eval(const alert_rule_t *rule, double value);

/* Evaluate a list of rules; returns the MOST SEVERE level that fires. */
alert_level_t alerts_eval_all(const alert_rule_t *rules, int n, double value);

/* Map a level to a severity word for display. */
const char *alerts_level_name(alert_level_t level);
