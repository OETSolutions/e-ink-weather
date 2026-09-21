/* Verifies the provisioning scan JSON builder cannot overrun its buffer.
 *
 * WHY THIS TEST EXISTS. This logic used to live in components/prov/prov_ap.c and had a heap
 * buffer overflow: `o += snprintf(out + o, 2048 - o, ...)` is only safe while `o` stays under
 * the cap, and snprintf returns the WOULD-BE length, so one entry too many pushed `o` past the
 * end and turned the next `2048 - o` into a huge size_t. An ESP-IDF component cannot be
 * host-tested, so nothing here could catch it and the build stayed green. Moved to lib/ the
 * property is now checked against real inputs, with canaries.
 *
 * The inputs reproduce the trigger: a full list of long non-Latin names, each of which
 * json_escape expands six-fold, overflowing the 2 KB buffer the scan handler used. */

#include "unity.h"
#include "provjson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The scan handler's buffer. The tests size their own, but this is the number that was
 * overflowed, so it is what the realistic cases below are run at. */
#define SCAN_CAP 2048
#define CANARY   0xA5

/* A guarded buffer: real space of `cap`, then a guard region that must stay untouched.
 *
 * THE GUARD IS VAST ON PURPOSE. The failure being guarded against is not a tidy one-byte
 * overrun — it is a runaway offset (snprintf returns the length it WOULD have written, so once
 * `o` passes `cap` every later size argument underflows to a huge size_t). With a small guard a
 * runaway write lands far past the buffer and is never seen, so the test would pass while the
 * bug was present. The guard is large enough to cover where a runaway actually writes, which is
 * how the negative-control run for this file caught the overflow rather than merely catching the
 * offset being stored. */
#define GUARD 8192

typedef struct {
    unsigned char *raw;
    char *buf;
    size_t cap;
} guarded_t;

static void guard_init(guarded_t *g, size_t cap)
{
    g->cap = cap;
    g->raw = malloc(cap + GUARD);
    memset(g->raw, CANARY, cap + GUARD);
    g->buf = (char *)g->raw;
    memset(g->buf, 0, cap);
}

static void guard_free(guarded_t *g)
{
    free(g->raw);
    g->raw = NULL;
}

/* 1 if any byte past the buffer was disturbed. */
static int guard_broken(const guarded_t *g)
{
    for (size_t i = g->cap; i < g->cap + GUARD; i++) {
        if (g->raw[i] != CANARY) return 1;
    }
    return 0;
}

void setUp(void) {}
void tearDown(void) {}

/* ---- escaping ------------------------------------------------------------------------ */

static void test_escape_plain_ascii_is_unchanged(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, provjson_escape("HomeNet2G", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("HomeNet2G", out);
}

static void test_escape_quotes_and_backslashes(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, provjson_escape("a\"b\\c", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", out);
}

static void test_escape_control_bytes_become_u_sequences(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, provjson_escape("a\tb", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\u0009b", out);
}

/* High bytes must be escaped, not passed through: a raw invalid UTF-8 byte makes the whole
 * document unparseable, so one oddly-named AP would take out every other network in the list. */
static void test_escape_high_bytes_become_u_sequences(void)
{
    char out[64];
    const char ssid[] = { (char)0xE4, (char)0xB8, (char)0xAD, '\0' };  /* UTF-8 "中" */
    TEST_ASSERT_EQUAL_INT(0, provjson_escape(ssid, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\\u00e4\\u00b8\\u00ad", out);
}

static void test_escape_always_terminates(void)
{
    char out[8];
    const char ssid[] = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";   /* expands well past 8 */
    TEST_ASSERT_EQUAL_INT(0, provjson_escape(ssid, out, sizeof(out)));
    /* Whatever it chose to keep, the result is a terminated C string inside the buffer. */
    TEST_ASSERT_TRUE(strlen(out) < sizeof(out));
}

static void test_escape_rejects_bad_out(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_INT(-1, provjson_escape("x", NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, provjson_escape("x", out, 0));
}

/* ---- document shape ------------------------------------------------------------------ */

static void test_empty_list_is_the_valid_empty_document(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_STRING("{\"list\":[]}", g.buf);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

static void test_single_entry_shape(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    provjson_list_begin(g.buf, g.cap, &o);
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "Home", -42, 1));
    provjson_list_end(g.buf, g.cap, &o);
    TEST_ASSERT_EQUAL_STRING("{\"list\":[{\"ssid\":\"Home\",\"rssi\":-42}]}", g.buf);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

static void test_two_entries_are_comma_separated(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    provjson_list_begin(g.buf, g.cap, &o);
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "A", -1, 1));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "B", -2, 0));
    provjson_list_end(g.buf, g.cap, &o);
    TEST_ASSERT_EQUAL_STRING("{\"list\":[{\"ssid\":\"A\",\"rssi\":-1},{\"ssid\":\"B\",\"rssi\":-2}]}",
                             g.buf);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

/* The document must parse. Checked with cJSON, the same parser the config app and the
 * firmware's own consumers use, so a quoting bug fails here rather than in the browser. */
static void test_document_parses_with_nasty_names(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "a\"b\\c", -10, 1));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "\xC3\xA9\xE4\xB8\xAD", -20, 0));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "tab\there", -30, 0));
    provjson_list_end(g.buf, g.cap, &o);

    extern void *cJSON_Parse(const char *);
    extern void  cJSON_Delete(void *);
    void *root = cJSON_Parse(g.buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "the scan document must be valid JSON");
    cJSON_Delete(root);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

/* ---- the overflow this file exists for ------------------------------------------------- */

/* 64 entries of 32-byte Latin names, capped at 24 by the caller. 24 * ~54 bytes fits easily —
 * this is the ordinary case and must produce a complete, valid list. */
static void test_24_max_length_latin_ssids_fit(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));

    char name[PROVJSON_MAX_SSID_BYTES + 1];
    memset(name, 'X', PROVJSON_MAX_SSID_BYTES);
    name[PROVJSON_MAX_SSID_BYTES] = '\0';

    int added = 0;
    for (int i = 0; i < 24; i++) {
        name[0] = (char)('A' + i);              /* distinct names, same length */
        const int r = provjson_list_add(g.buf, g.cap, &o, name, -30 - i, i == 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, r, "24 max-length Latin SSIDs must all fit in 2 KB");
        added++;
    }
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_INT(24, added);
    TEST_ASSERT_FALSE(guard_broken(&g));
    /* The closing brace is present, so no entry was dropped. */
    TEST_ASSERT_EQUAL_STRING("]}", g.buf + strlen(g.buf) - 2);
    guard_free(&g);
}

/* THE REGRESSION CASE. 24 names of 32 high bytes each escape to 192 bytes apiece, so a full
 * list wants ~5 KB inside a 2 KB buffer. Before the room check this wrote off the end of the
 * heap block. Now the writer must stop adding entries early and still emit a valid, terminated,
 * complete-so-far document, with the canary intact. */
static void test_non_latin_names_stop_early_without_overrun(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));

    /* 32 bytes, each expanding six-fold. */
    char name[PROVJSON_MAX_SSID_BYTES + 1];
    for (int i = 0; i < PROVJSON_MAX_SSID_BYTES; i++) name[i] = (char)(0xE0 + i);
    name[PROVJSON_MAX_SSID_BYTES] = '\0';

    int added = 0, refused = 0;
    for (int i = 0; i < 24; i++) {
        name[0] = (char)(0xE0 + (i % 16));      /* distinct enough to not matter here */
        const int r = provjson_list_add(g.buf, g.cap, &o, name, -40 - i, i == 0);
        if (r == 0) added++;
        else if (r == 1) { refused++; break; }  /* the designed stop */
        else TEST_FAIL_MESSAGE("add returned an error");
    }
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));

    TEST_ASSERT_MESSAGE(refused == 1, "the writer must refuse an entry it cannot fit");
    TEST_ASSERT_MESSAGE(added > 0, "at least some entries must fit");
    TEST_ASSERT_MESSAGE(added < 24, "24 six-fold names cannot all fit in 2 KB");
    TEST_ASSERT_FALSE_MESSAGE(guard_broken(&g), "the buffer past its end was written to");
    TEST_ASSERT_TRUE_MESSAGE(strlen(g.buf) < g.cap, "the document must be terminated in bounds");
    TEST_ASSERT_EQUAL_STRING("]}", g.buf + strlen(g.buf) - 2);

    extern void *cJSON_Parse(const char *);
    extern void  cJSON_Delete(void *);
    void *root = cJSON_Parse(g.buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(root, "even a truncated list must be valid JSON");
    cJSON_Delete(root);
    guard_free(&g);
}

/* The worst single entry — 32 six-fold bytes — plus the closing brace must fit in the buffer
 * by itself. If this ever fails, an unfittable entry has become a permanent empty list. */
static void test_one_worst_case_entry_fits_alone(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));

    char name[PROVJSON_MAX_SSID_BYTES + 1];
    for (int i = 0; i < PROVJSON_MAX_SSID_BYTES; i++) name[i] = (char)(0x80 + i);
    name[PROVJSON_MAX_SSID_BYTES] = '\0';

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, provjson_list_add(g.buf, g.cap, &o, name, -99, 1),
                                  "a single worst-case SSID must fit in the scan buffer");
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));
    TEST_ASSERT_FALSE(guard_broken(&g));

    extern void *cJSON_Parse(const char *);
    extern void  cJSON_Delete(void *);
    void *root = cJSON_Parse(g.buf);
    TEST_ASSERT_NOT_NULL(root);
    cJSON_Delete(root);
    guard_free(&g);
}

/* A rssi of -100 is the longest integer the field can hold; the room check must account for
 * the negative sign and three digits rather than assuming -1 or -42. */
static void test_rssi_width_is_accounted_for(void)
{
    guarded_t g;
    guard_init(&g, SCAN_CAP);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_add(g.buf, g.cap, &o, "weak", -100, 1));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_STRING("{\"list\":[{\"ssid\":\"weak\",\"rssi\":-100}]}", g.buf);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

/* ---- argument hostility ---------------------------------------------------------------- */

static void test_bad_arguments_are_rejected(void)
{
    char buf[128];
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_begin(NULL, sizeof(buf), &o));
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_begin(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_begin(buf, 4, &o));   /* too small for `{"list":[` */

    o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(buf, sizeof(buf), &o));
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_add(buf, sizeof(buf), &o, NULL, -1, 1));
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_add(NULL, sizeof(buf), &o, "x", -1, 1));
    TEST_ASSERT_EQUAL_INT(-1, provjson_list_end(NULL, sizeof(buf), &o));
}

/* A cap big enough for the opening but not for any entry: the first add must be refused and
 * the result must still be the valid empty document, not a half-written fragment. */
static void test_cap_too_small_for_any_entry_still_yields_empty_document(void)
{
    guarded_t g;
    guard_init(&g, 20);
    size_t o = 0;
    TEST_ASSERT_EQUAL_INT(0, provjson_list_begin(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_INT(1, provjson_list_add(g.buf, g.cap, &o, "Home", -42, 1));
    TEST_ASSERT_EQUAL_INT(0, provjson_list_end(g.buf, g.cap, &o));
    TEST_ASSERT_EQUAL_STRING("{\"list\":[]}", g.buf);
    TEST_ASSERT_FALSE(guard_broken(&g));
    guard_free(&g);
}

/* Many entries against a range of caps: nothing may ever escape, and the offset must always
 * stay inside. This is the sweep that makes the bounds property a checked invariant rather
 * than one hand-picked case. */
static void test_no_cap_in_range_can_be_overrun(void)
{
    for (size_t cap = 12; cap <= 4096; cap += 7) {
        guarded_t g;
        guard_init(&g, cap);
        size_t o = 0;
        if (provjson_list_begin(g.buf, g.cap, &o) != 0) { guard_free(&g); continue; }

        int first = 1;
        for (int i = 0; i < 24; i++) {
            char name[PROVJSON_MAX_SSID_BYTES + 1];
            memset(name, (char)(0x80 + (i % 60)), PROVJSON_MAX_SSID_BYTES);
            name[PROVJSON_MAX_SSID_BYTES] = '\0';
            if (provjson_list_add(g.buf, g.cap, &o, name, -1 - i, first) != 0) break;
            first = 0;
        }
        provjson_list_end(g.buf, g.cap, &o);

        if (guard_broken(&g)) {
            char msg[80];
            snprintf(msg, sizeof(msg), "overran the buffer at cap=%u", (unsigned)cap);
            TEST_FAIL_MESSAGE(msg);
        }
        TEST_ASSERT_TRUE_MESSAGE(o < cap, "the offset must stay below the cap");
        guard_free(&g);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_escape_plain_ascii_is_unchanged);
    RUN_TEST(test_escape_quotes_and_backslashes);
    RUN_TEST(test_escape_control_bytes_become_u_sequences);
    RUN_TEST(test_escape_high_bytes_become_u_sequences);
    RUN_TEST(test_escape_always_terminates);
    RUN_TEST(test_escape_rejects_bad_out);
    RUN_TEST(test_empty_list_is_the_valid_empty_document);
    RUN_TEST(test_single_entry_shape);
    RUN_TEST(test_two_entries_are_comma_separated);
    RUN_TEST(test_document_parses_with_nasty_names);
    RUN_TEST(test_24_max_length_latin_ssids_fit);
    RUN_TEST(test_non_latin_names_stop_early_without_overrun);
    RUN_TEST(test_one_worst_case_entry_fits_alone);
    RUN_TEST(test_rssi_width_is_accounted_for);
    RUN_TEST(test_bad_arguments_are_rejected);
    RUN_TEST(test_cap_too_small_for_any_entry_still_yields_empty_document);
    RUN_TEST(test_no_cap_in_range_can_be_overrun);
    return UNITY_END();
}
