#include "alerts.h"
#include <math.h>

static int cmp_fires(alert_op_t op, double v, double t)
{
    switch (op) {
    case ALERT_OP_GT:  return v >  t;
    case ALERT_OP_GTE: return v >= t;
    case ALERT_OP_LT:  return v <  t;
    case ALERT_OP_LTE: return v <= t;
    case ALERT_OP_EQ:  return v == t;
    case ALERT_OP_NE:  return v != t;
    }
    return 0;
}

alert_level_t alerts_eval(const alert_rule_t *rule, double value)
{
    if (!rule) return ALERT_NONE;
    if (!isfinite(value) || !isfinite(rule->threshold)) return ALERT_NONE;
    return cmp_fires(rule->op, value, rule->threshold) ? rule->level : ALERT_NONE;
}

alert_level_t alerts_eval_all(const alert_rule_t *rules, int n, double value)
{
    alert_level_t worst = ALERT_NONE;
    for (int i = 0; i < n; i++) {
        alert_level_t l = alerts_eval(&rules[i], value);
        if (l > worst) worst = l;
    }
    return worst;
}

const char *alerts_level_name(alert_level_t level)
{
    switch (level) {
    case ALERT_ADVISORY: return "advisory";
    case ALERT_WARNING:  return "warning";
    case ALERT_SEVERE:   return "severe";
    default:             return "none";
    }
}
