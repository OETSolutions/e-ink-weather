#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "apiauth.h"

void setUp(void) {}
void tearDown(void) {}

static void test_accepts_correct_bearer(void)
{
    TEST_ASSERT_EQUAL_INT(1, apiauth_header_matches("Bearer abc123", "abc123"));
}

/* RFC 7235 makes the auth scheme case-insensitive, and clients emit both spellings. */
static void test_scheme_is_case_insensitive(void)
{
    TEST_ASSERT_EQUAL_INT(1, apiauth_header_matches("bearer abc123", "abc123"));
    TEST_ASSERT_EQUAL_INT(1, apiauth_header_matches("BEARER abc123", "abc123"));
    TEST_ASSERT_EQUAL_INT(1, apiauth_header_matches("BeArEr abc123", "abc123"));
}

/* The TOKEN is an opaque byte string and must NOT be case-folded with the scheme. A token
 * compare that folded case would accept a token that is not the one that was set. */
static void test_token_is_case_sensitive(void)
{
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer ABC123", "abc123"));
}

static void test_rejects_wrong_token(void)
{
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer abc124", "abc123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer ab", "abc123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer abc1234", "abc123"));
}

static void test_rejects_missing_or_malformed_header(void)
{
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches(NULL, "abc123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("", "abc123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("abc123", "abc123"));       /* no scheme */
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Basic abc123", "abc123")); /* wrong scheme */
    /* Two spaces: the token would begin with a space, which can never match a usable token
     * (apiauth_token_is_usable rejects spaces), so this must be refused. */
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer  abc123", "abc123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer", "abc123"));
}

/* The critical one: a device with no token configured must NOT accept an empty bearer, or
 * the "auth off" state could be forged into an "auth on, empty key" state. */
static void test_empty_expected_never_matches(void)
{
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer ", ""));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer abc", ""));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches("Bearer ", NULL));
    TEST_ASSERT_EQUAL_INT(0, apiauth_header_matches(NULL, NULL));
}

/* The safe state: enabled but no token set must require NOTHING, because the alternative is
 * demanding a credential that does not exist — an unrecoverable lockout. */
static void test_required_only_when_enabled_and_token_set(void)
{
    TEST_ASSERT_EQUAL_INT(0, apiauth_required(0, "abc123"));   /* off */
    TEST_ASSERT_EQUAL_INT(0, apiauth_required(1, ""));         /* on, no token: must not lock */
    TEST_ASSERT_EQUAL_INT(0, apiauth_required(1, NULL));       /* on, no token */
    TEST_ASSERT_EQUAL_INT(1, apiauth_required(1, "abc123"));   /* on, token set */
}

static void test_token_usability(void)
{
    TEST_ASSERT_EQUAL_INT(1, apiauth_token_is_usable("abc123"));
    TEST_ASSERT_EQUAL_INT(1, apiauth_token_is_usable("a-b_c.d:e"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable(""));
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable(NULL));
    /* A space inside could never survive the header parse, so it must not be storable —
     * otherwise the owner sets a token that then rejects every request. */
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable("abc 123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable("abc\t123"));
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable("abc\n123"));
}

/* Bounds. The documented contract is "longest accepted token is APIAUTH_TOKEN_MAX", so
 * exactly that length is usable and one character more is not — the boundary is the whole
 * point of the test, so both sides are built explicitly. */
static void test_token_length_bound(void)
{
    char t[APIAUTH_TOKEN_MAX + 8];

    memset(t, 'a', sizeof(t));
    t[APIAUTH_TOKEN_MAX] = '\0';                 /* length == MAX */
    TEST_ASSERT_EQUAL_INT(APIAUTH_TOKEN_MAX, (int)strlen(t));
    TEST_ASSERT_EQUAL_INT(1, apiauth_token_is_usable(t));

    t[APIAUTH_TOKEN_MAX] = 'a';                  /* restore, then go one over */
    t[APIAUTH_TOKEN_MAX + 1] = '\0';
    TEST_ASSERT_EQUAL_INT(APIAUTH_TOKEN_MAX + 1, (int)strlen(t));
    TEST_ASSERT_EQUAL_INT(0, apiauth_token_is_usable(t));
}

/* A deterministic stand-in for rand(), so the token generator is testable. */
static unsigned s_seq;
static unsigned fake_rand(void) { return 0x10u + (s_seq++); }

static void test_make_token_is_hex_and_terminated(void)
{
    s_seq = 0;
    char t[17];
    apiauth_make_token(t, 16, fake_rand);
    TEST_ASSERT_EQUAL_INT(16, (int)strlen(t));
    for (const char *p = t; *p; p++) {
        TEST_ASSERT_TRUE((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'));
    }
    /* And it round-trips: the generated token must be one the device would accept. */
    TEST_ASSERT_EQUAL_INT(1, apiauth_token_is_usable(t));
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Bearer %s", t);
    TEST_ASSERT_EQUAL_INT(1, apiauth_header_matches(hdr, t));
}

static void test_make_token_handles_odd_length(void)
{
    s_seq = 0;
    char t[8];
    apiauth_make_token(t, 7, fake_rand);      /* odd: exercises the trailing nibble */
    TEST_ASSERT_EQUAL_INT(7, (int)strlen(t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_correct_bearer);
    RUN_TEST(test_scheme_is_case_insensitive);
    RUN_TEST(test_token_is_case_sensitive);
    RUN_TEST(test_rejects_wrong_token);
    RUN_TEST(test_rejects_missing_or_malformed_header);
    RUN_TEST(test_empty_expected_never_matches);
    RUN_TEST(test_required_only_when_enabled_and_token_set);
    RUN_TEST(test_token_usability);
    RUN_TEST(test_token_length_bound);
    RUN_TEST(test_make_token_is_hex_and_terminated);
    RUN_TEST(test_make_token_handles_odd_length);
    return UNITY_END();
}
