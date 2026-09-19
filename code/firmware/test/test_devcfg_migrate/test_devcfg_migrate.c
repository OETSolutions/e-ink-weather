#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "devcfg.h"

void setUp(void) {}
void tearDown(void) {}

static void test_same_version_is_passthrough(void)
{
    const char *in = "{\"schemaVersion\":1}";
    char *out = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, 1, in, &out));
    TEST_ASSERT_EQUAL_STRING(in, out);
    free(out);
}

static void test_unknown_newer_version_is_refused(void)
{
    char *out = NULL;
    /* A config from the future must be refused, not silently mis-read (FR-26b). */
    TEST_ASSERT_NOT_EQUAL(0, devcfg_migrate(99, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":99}", &out));
    TEST_ASSERT_NULL(out);
}

static void test_migrate_is_idempotent(void)
{
    char *once = NULL, *twice = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":1}", &once));
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION, once, &twice));
    TEST_ASSERT_EQUAL_STRING(once, twice);
    free(once); free(twice);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_version_is_passthrough);
    RUN_TEST(test_unknown_newer_version_is_refused);
    RUN_TEST(test_migrate_is_idempotent);
    return UNITY_END();
}
