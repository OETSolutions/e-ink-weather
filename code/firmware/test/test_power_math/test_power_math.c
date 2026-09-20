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


/* ---- the powerMode override (FR-8) ---- */

/* The default must be the inference, so an absent field changes nothing. */
static void test_auto_passes_the_detection_through(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY,
        power_apply_mode(POWER_SOURCE_BATTERY, POWER_MODE_AUTO));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,
        power_apply_mode(POWER_SOURCE_USB, POWER_MODE_AUTO));
}

/* The whole reason the field exists: a full resting cell reads as mains, and the user
 * corrects it. */
static void test_battery_mode_overrides_a_mains_inference(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY,
        power_apply_mode(POWER_SOURCE_USB, POWER_MODE_BATTERY));
}

static void test_always_on_overrides_a_battery_inference(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB,
        power_apply_mode(POWER_SOURCE_BATTERY, POWER_MODE_ALWAYS_ON));
}

/* An unrecognised mode must fall back to the INFERENCE, not invent a behaviour. Passing
 * garbage through the enum is exactly what a config from a newer web app would do. */
static void test_invalid_mode_falls_back_to_detection(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_BATTERY, power_apply_mode(POWER_SOURCE_BATTERY, 999));
    TEST_ASSERT_EQUAL_INT(POWER_SOURCE_USB, power_apply_mode(POWER_SOURCE_USB, -1));
}

static void test_mode_strings_parse(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, power_mode_from_string("auto"));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_ALWAYS_ON, power_mode_from_string("always-on"));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_BATTERY, power_mode_from_string("battery"));
}

/* Anything else is 'auto' — never a fixed mode the user did not choose. */
static void test_unknown_mode_string_is_auto(void)
{
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, power_mode_from_string(""));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, power_mode_from_string(NULL));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, power_mode_from_string("ALWAYS-ON"));  /* case-sensitive */
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, power_mode_from_string("mains"));
}

static void test_mode_validity(void)
{
    TEST_ASSERT_EQUAL_INT(1, power_mode_is_valid(POWER_MODE_AUTO));
    TEST_ASSERT_EQUAL_INT(1, power_mode_is_valid(POWER_MODE_ALWAYS_ON));
    TEST_ASSERT_EQUAL_INT(1, power_mode_is_valid(POWER_MODE_BATTERY));
    TEST_ASSERT_EQUAL_INT(0, power_mode_is_valid(3));
    TEST_ASSERT_EQUAL_INT(0, power_mode_is_valid(-1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_divider_ratio);
    RUN_TEST(test_vbat_recovered_from_node_voltage);
    RUN_TEST(test_raw_to_volts_uses_denominator_not_max);
    RUN_TEST(test_classification);
    RUN_TEST(test_full_charge_but_discharging_is_battery_not_usb);
    RUN_TEST(test_auto_passes_the_detection_through);
    RUN_TEST(test_battery_mode_overrides_a_mains_inference);
    RUN_TEST(test_always_on_overrides_a_battery_inference);
    RUN_TEST(test_invalid_mode_falls_back_to_detection);
    RUN_TEST(test_mode_strings_parse);
    RUN_TEST(test_unknown_mode_string_is_auto);
    RUN_TEST(test_mode_validity);
    return UNITY_END();
}
