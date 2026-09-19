#include <string.h>
#include "unity.h"
#include "layout.h"

void setUp(void) {}
void tearDown(void) {}

static void test_layout_version_is_nonempty(void)
{
    const char *v = layout_version();
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_TRUE(strlen(v) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layout_version_is_nonempty);
    return UNITY_END();
}
