#pragma once

#define DEVCFG_SCHEMA_VERSION 1

/* Upgrade a config JSON document from `from` to `to`.
 * Returns 0 on success and sets *out to a malloc'd string the caller frees.
 * Returns non-zero and leaves *out NULL on any unsupported/unknown version. */
int devcfg_migrate(int from, int to, const char *in_json, char **out_json);
