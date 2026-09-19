#pragma once

/* IF-3: one value contract shared by every data source, so widgets are source-agnostic
 * and never branch on where a number came from. */

typedef enum {
    DATASRC_OK = 0,
    DATASRC_ERR_PARSE,         /* the payload was not understood */
    DATASRC_ERR_UNAVAILABLE,   /* entity present but state is "unavailable"/"unknown" */
    DATASRC_ERR_STALE,         /* reading older than the allowed age */
    DATASRC_ERR_NOT_FOUND      /* the field/index asked for is not in this response */
} datasrc_status_t;

typedef struct {
    datasrc_status_t status;
    double           value;
    int              is_numeric;   /* 0 for text-valued sources (e.g. condition) */
    char             text[64];
    long             observed_at;  /* unix seconds, 0 if unknown */
} datasrc_value_t;
