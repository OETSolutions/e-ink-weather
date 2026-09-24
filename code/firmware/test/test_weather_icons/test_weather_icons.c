#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "weather_icons.h"

/* The OpenWeatherMap icon-code mapping (FR-1's "icons").
 *
 * WHY THIS MATTERS ENOUGH TO TEST: the icon is drawn from a code the API supplies, and every
 * failure mode here is SILENT on the glass. A code mapped to the wrong icon shows plausible
 * wrong weather (a sun on a rainy day reads as a bug in the forecast, not in the firmware); a
 * code mapped to UNKNOWN simply draws nothing, leaving an empty box the user cannot explain.
 * The mapping is pure, so it is verified exhaustively here rather than on the panel.
 *
 * The reference is OWM's documented icon list (01..50, d/n), which is exactly what
 * owm_parse_current_field() passes through as text. */

void setUp(void) {}
void tearDown(void) {}

/* Every code the API documents maps to a real icon — never UNKNOWN. If OWM adds a code this
 * set does not cover, that code lands on UNKNOWN and the widget draws nothing, which is the
 * correct failure (no invented weather) but a thing to notice; this test pins the documented
 * set so a regression in the range logic is caught. */
static void test_every_documented_code_maps(void)
{
    static const char *CODES[] = {
        "01d", "01n", "02d", "02n", "03d", "03n", "04d", "04n",
        "09d", "09n", "10d", "10n", "11d", "11n", "13d", "13n", "50d", "50n",
    };
    for (size_t i = 0; i < sizeof(CODES) / sizeof(CODES[0]); i++) {
        const int idx = weather_icon_index(CODES[i]);
        TEST_ASSERT_TRUE_MESSAGE(idx >= 0, CODES[i]);
        TEST_ASSERT_TRUE_MESSAGE(idx < WEATHER_ICON_COUNT, CODES[i]);
    }
}

/* The day/night split applies ONLY to the two conditions that look different after dark. This
 * is the whole reason the set is nine icons and not eighteen, and it is what keeps a sun off
 * the glass at 2 a.m. */
static void test_day_night_split_only_where_it_matters(void)
{
    TEST_ASSERT_EQUAL(WEATHER_ICON_CLEAR,       weather_icon_index("01d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_CLEAR_NIGHT, weather_icon_index("01n"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_PARTLY,       weather_icon_index("02d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_PARTLY_NIGHT, weather_icon_index("02n"));

    /* Cloud, rain, storm, snow and fog render identically day and night. */
    TEST_ASSERT_EQUAL(weather_icon_index("04d"), weather_icon_index("04n"));
    TEST_ASSERT_EQUAL(weather_icon_index("10d"), weather_icon_index("10n"));
    TEST_ASSERT_EQUAL(weather_icon_index("11d"), weather_icon_index("11n"));
    TEST_ASSERT_EQUAL(weather_icon_index("13d"), weather_icon_index("13n"));
    TEST_ASSERT_EQUAL(weather_icon_index("50d"), weather_icon_index("50n"));
}

/* OWM's grouping: 02 (few clouds) keeps the sun; 03 and 04 do not. Getting this wrong is the
 * difference between "partly cloudy" and "overcast" on the glass. */
static void test_cloud_groups(void)
{
    TEST_ASSERT_EQUAL(WEATHER_ICON_PARTLY, weather_icon_index("02d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_CLOUDY, weather_icon_index("03d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_CLOUDY, weather_icon_index("04d"));
    /* 03/04 must NOT be treated as partly — the sun peeking through a broken cloud is exactly
     * the reading 02 carries and 03/04 do not. */
    TEST_ASSERT_NOT_EQUAL(WEATHER_ICON_PARTLY, weather_icon_index("03d"));
    TEST_ASSERT_NOT_EQUAL(WEATHER_ICON_PARTLY, weather_icon_index("04d"));
}

/* Both shower (09) and rain (10) collapse to the rain icon; both mist (50) groups map to fog. */
static void test_group_collapse(void)
{
    TEST_ASSERT_EQUAL(WEATHER_ICON_RAIN, weather_icon_index("09d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_RAIN, weather_icon_index("10d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_FOG,  weather_icon_index("50d"));
}

/* Anything that is not a usable code is UNKNOWN — and UNKNOWN draws nothing rather than a
 * guessed icon. An invented sun for an unparseable code is worse than an empty box, because the
 * user would believe it. */
static void test_garbage_is_unknown(void)
{
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index(NULL));
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index(""));
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index("d"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index("4n"));   /* one digit */
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index("abc"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index("99d"));  /* not a group */
    TEST_ASSERT_EQUAL(WEATHER_ICON_UNKNOWN, weather_icon_index("-4d"));
}

/* A code with no d/n letter is treated as day. This is malformed input, and day is the
 * conservative choice: showing a sun for a clear sky is less jarring than assuming night. */
static void test_missing_suffix_defaults_to_day(void)
{
    TEST_ASSERT_EQUAL(WEATHER_ICON_CLEAR, weather_icon_index("01"));
    TEST_ASSERT_EQUAL(WEATHER_ICON_PARTLY, weather_icon_index("02"));
}

/* The generated data must line up with the enum: every index in range has a real, non-empty
 * bitmap of the declared size. This is what catches a generator/enum drift — appending an icon
 * in the generator without updating WEATHER_ICON_COUNT would otherwise leave the array short
 * and the renderer reading past it. */
static void test_generated_set_matches_the_enum(void)
{
    for (int i = 0; i < WEATHER_ICON_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(weather_icons[i].bits);
        TEST_ASSERT_TRUE(weather_icons[i].w > 0);
        TEST_ASSERT_TRUE(weather_icons[i].h > 0);
    }
    /* The UNKNOWN sentinel is not a drawable index. */
    TEST_ASSERT_TRUE(WEATHER_ICON_UNKNOWN < 0);
}

/* Every icon must actually have ink in it. A generator bug that emitted an all-white bitmap
 * would produce a valid-looking icon that draws nothing — the exact "reports success, panel is
 * blank" failure this project has hit before. */
static void test_every_icon_has_ink(void)
{
    for (int i = 0; i < WEATHER_ICON_COUNT; i++) {
        const weather_icon_t *ic = &weather_icons[i];
        const int pitch = (ic->w + 7) / 8;
        const size_t bytes = (size_t)pitch * ic->h;
        size_t ink = 0;
        for (size_t b = 0; b < bytes; b++) {
            for (int bit = 0; bit < 8; bit++) {
                if (ic->bits[b] & (0x80 >> bit)) ink++;
            }
        }
        char msg[64];
        snprintf(msg, sizeof(msg), "icon %d has no ink", i);
        TEST_ASSERT_TRUE_MESSAGE(ink > 0, msg);
        /* And not so much that it is a filled square — a drawing, not a block. */
        snprintf(msg, sizeof(msg), "icon %d is a filled block (%zu/%zu)", i, ink, bytes * 8);
        TEST_ASSERT_TRUE_MESSAGE(ink < bytes * 8 / 2, msg);
    }
}

/* THE ICON SET MUST BE HIGH-RESOLUTION ENOUGH FOR THE BOX IT IS DRAWN INTO.
 *
 * Reported defect: "the icons are very blocky" with the crescent moon a staircase. The renderer
 * fits an icon to the smaller side of its box and scales by NEAREST NEIGHBOUR, so a source smaller
 * than the box magnifies each pixel into a block. At 64 px that was invisible in the default
 * layout's 62 px box and obvious the moment a user drew a box at the panel's own scale.
 *
 * WHY THIS IS A TEST AND NOT A COMMENT: the size is a number in a generator, and nothing else in
 * the build notices it — the icons render, the mapping is right, and the panel just looks worse.
 * A silent quality regression is exactly what a threshold here catches. The bar is deliberately
 * BELOW the chosen 192 so it does not fail on a future deliberate change, but far ABOVE 64 so
 * reverting the fix cannot pass. */
static void test_icons_are_high_resolution_enough_not_to_look_blocky(void)
{
    for (int i = 0; i < WEATHER_ICON_COUNT; i++) {
        const weather_icon_t *ic = &weather_icons[i];
        char msg[96];
        snprintf(msg, sizeof(msg), "icon %d is %dx%d; a box drawn larger than this magnifies "
                                   "every pixel into a block", i, ic->w, ic->h);
        TEST_ASSERT_TRUE_MESSAGE(ic->w >= 128, msg);
        TEST_ASSERT_TRUE_MESSAGE(ic->h >= 128, msg);
        /* SQUARE, because the renderer fits by the SMALLER side: a non-square icon would be
         * letterboxed in one direction and its drawing would sit off-centre from the box. */
        TEST_ASSERT_EQUAL_INT(ic->w, ic->h);
    }
}

/* The set is uniform, so the generator emitted one geometry. A single icon at a different size
 * would render at a different scale from its neighbours in the same box — a sun visibly larger
 * than the cloud beside it, with no cause the user could see. */
static void test_every_icon_shares_one_size(void)
{
    const int w = weather_icons[0].w, h = weather_icons[0].h;
    for (int i = 0; i < WEATHER_ICON_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(w, weather_icons[i].w);
        TEST_ASSERT_EQUAL_INT(h, weather_icons[i].h);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_documented_code_maps);
    RUN_TEST(test_day_night_split_only_where_it_matters);
    RUN_TEST(test_cloud_groups);
    RUN_TEST(test_group_collapse);
    RUN_TEST(test_garbage_is_unknown);
    RUN_TEST(test_missing_suffix_defaults_to_day);
    RUN_TEST(test_generated_set_matches_the_enum);
    RUN_TEST(test_every_icon_has_ink);
    RUN_TEST(test_icons_are_high_resolution_enough_not_to_look_blocky);
    RUN_TEST(test_every_icon_shares_one_size);
    return UNITY_END();
}
