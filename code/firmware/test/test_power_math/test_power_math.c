#include <math.h>
#include "unity.h"
#include "power.h"

void setUp(void) {}
void tearDown(void) {}

/* The divider ratio is a verified schematic fact: R8=300k top, R9=1M bottom. */
static void test_divider_ratio(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.7692307692307693, VBAT_DIVIDER_RATIO);
}

static void test_vbat_recovered_from_node_voltage(void)
{
    /* A 4.2 V battery presents 4.2 * ratio at the sense node... */
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 4.2, power_vbat_from_vref(4.2 * VBAT_DIVIDER_RATIO));
    /* ...so 3.0 V at the node implies ~3.9 V battery. */
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 3.9, power_vbat_from_vref(3.0));
}

static void test_raw_to_volts_uses_denominator_not_max(void)
{
    /* 12-bit ADC: half scale is 2048/4096, NOT 2048/4095. */
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.65, power_vref_from_raw(2048, 4096, 3.3));
}

static void test_classification(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,     power_classify(4.20,  0.00));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,     power_classify(4.20,  0.01));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.00, -0.01));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(3.70,  0.0));
    /* An unreadable ADC must be UNKNOWN, never a guess. */
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_UNKNOWN, power_classify(NAN, 0.0));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_UNKNOWN, power_classify(0.0, 0.0));
}

/* The trend term is what keeps a charged pack from reading as mains: a full cell at
 * 4.2 V that is FALLING is discharging, so it is on battery despite the high voltage.
 * This is the case that a voltage-only rule would get wrong — if you ever "simplify"
 * power_classify to `vbat >= 4.15`, this test is what must stop you. */
static void test_full_charge_but_discharging_is_battery_not_usb(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.20, -0.02));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_classify(4.19, -0.05));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divider_ratio);
    RUN_TEST(test_vbat_recovered_from_node_voltage);
    RUN_TEST(test_raw_to_volts_uses_denominator_not_max);
    RUN_TEST(test_classification);
    RUN_TEST(test_full_charge_but_discharging_is_battery_not_usb);
    return UNITY_END();
}
