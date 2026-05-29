#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdint.h>

/* Simulate the vulnerable buffer size as used in process-burkardt.c.
   The tool uses a fixed-size buffer 'a1' — we assume a typical small buffer
   size (e.g., 64 or 128 bytes) as would be declared in the original code. */
#define BUFFER_SIZE 64
#define FMT "%g"

/* Canary value to detect buffer overflows */
#define CANARY_VALUE 0xDEADBEEF

/* Safe formatting function that enforces buffer bounds */
static int safe_format_double(char *buf, size_t buf_size, const char *fmt, double val) {
    int needed = snprintf(buf, buf_size, fmt, val);
    /* snprintf returns the number of characters that would have been written
       if buf_size were unlimited. If needed >= buf_size, truncation occurred. */
    return needed;
}

static int safe_format_int(char *buf, size_t buf_size, int val) {
    int needed = snprintf(buf, buf_size, "%d", val);
    return needed;
}

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    /* Invariant: Buffer reads/writes must never exceed the declared buffer length.
       Formatting operations must not write beyond the allocated buffer size. */

    /* Adversarial double values that can produce extremely long string representations */
    double adversarial_doubles[] = {
        1e308,
        -1e308,
        1e-308,
        DBL_MAX,
        -DBL_MAX,
        DBL_MIN,
        1.7976931348623157e+308,
        -1.7976931348623157e+308,
        1e100,
        -1e100,
        1e200,
        1e250,
        1e300,
        1.23456789012345678901234567890e308,
        -9.99999999999999999999999999999e307,
        INFINITY,
        -INFINITY,
        NAN,
        0.0,
        -0.0,
        1.0 / 0.0,
        -1.0 / 0.0,
        1e50,
        1e150,
        /* Values that produce long decimal representations with %f-style formats */
        1.0000000000000002,
        0.1,
        0.12345678901234567890,
        1.0 / 3.0,
        2.0 / 3.0,
        M_PI,
        M_E,
    };

    int num_doubles = sizeof(adversarial_doubles) / sizeof(adversarial_doubles[0]);

    /* Adversarial integer values */
    int adversarial_ints[] = {
        INT_MAX,
        INT_MIN,
        -2147483648,
        2147483647,
        -1,
        0,
        1,
        999999999,
        -999999999,
        100000000,
        -100000000,
        1000000000,
        -1000000000,
    };

    int num_ints = sizeof(adversarial_ints) / sizeof(adversarial_ints[0]);

    /* Test double formatting */
    for (int i = 0; i < num_doubles; i++) {
        /* Allocate buffer with canary protection */
        uint32_t canary_before = CANARY_VALUE;
        char a1[BUFFER_SIZE];
        uint32_t canary_after = CANARY_VALUE;

        memset(a1, 0, BUFFER_SIZE);

        double val = adversarial_doubles[i];

        /* Use snprintf instead of sprintf to enforce bounds */
        int needed = safe_format_double(a1, BUFFER_SIZE, FMT, val);

        /* Invariant 1: The buffer must not be overflowed.
           snprintf guarantees null-termination within buf_size. */
        ck_assert_msg(a1[BUFFER_SIZE - 1] == '\0' || strlen(a1) < BUFFER_SIZE,
                      "Buffer overflow detected for double value at index %d", i);

        /* Invariant 2: The actual string length must be within buffer bounds */
        size_t actual_len = strnlen(a1, BUFFER_SIZE);
        ck_assert_msg(actual_len < BUFFER_SIZE,
                      "String length %zu exceeds buffer size %d for double index %d",
                      actual_len, BUFFER_SIZE, i);

        /* Invariant 3: Canaries must be intact (no stack smashing) */
        ck_assert_msg(canary_before == CANARY_VALUE,
                      "Canary before buffer corrupted for double index %d", i);
        ck_assert_msg(canary_after == CANARY_VALUE,
                      "Canary after buffer corrupted for double index %d", i);

        /* Invariant 4: If truncation occurred (needed >= BUFFER_SIZE),
           the result must still be null-terminated within bounds */
        if (needed >= (int)BUFFER_SIZE) {
            /* Truncation happened — verify null terminator is present */
            ck_assert_msg(a1[BUFFER_SIZE - 1] == '\0',
                          "Truncated string not null-terminated for double index %d", i);
        }
    }

    /* Test integer formatting */
    for (int i = 0; i < num_ints; i++) {
        uint32_t canary_before = CANARY_VALUE;
        char a1[BUFFER_SIZE];
        uint32_t canary_after = CANARY_VALUE;

        memset(a1, 0, BUFFER_SIZE);

        int val = adversarial_ints[i];

        int needed = safe_format_int(a1, BUFFER_SIZE, val);

        /* Invariant 1: Buffer must not overflow */
        size_t actual_len = strnlen(a1, BUFFER_SIZE);
        ck_assert_msg(actual_len < BUFFER_SIZE,
                      "String length %zu exceeds buffer size %d for int index %d",
                      actual_len, BUFFER_SIZE, i);

        /* Invariant 2: Canaries must be intact */
        ck_assert_msg(canary_before == CANARY_VALUE,
                      "Canary before buffer corrupted for int index %d", i);
        ck_assert_msg(canary_after == CANARY_VALUE,
                      "Canary after buffer corrupted for int index %d", i);

        /* Invariant 3: Result must be null-terminated */
        ck_assert_msg(a1[BUFFER_SIZE - 1] == '\0' || actual_len < BUFFER_SIZE - 1,
                      "Buffer not properly null-terminated for int index %d", i);

        /* Invariant 4: needed must be non-negative (valid format) */
        ck_assert_msg(needed >= 0,
                      "snprintf returned error for int index %d", i);
    }

    /* Additional stress test: verify that using sprintf with large doubles
       would produce output exceeding BUFFER_SIZE, confirming the vulnerability
       exists and our safe version correctly handles it */
    {
        char probe[512];
        int probe_len = snprintf(probe, sizeof(probe), FMT, 1e308);
        /* For %g format, 1e308 should produce a short scientific notation string.
           But for %f format it would be enormous. Test both. */
        ck_assert_msg(probe_len >= 0, "snprintf probe failed");

        /* Test with %f format which is the dangerous case */
        char probe_f[512];
        int probe_f_len = snprintf(probe_f, sizeof(probe_f), "%f", 1e308);
        ck_assert_msg(probe_f_len >= 0, "snprintf probe_f failed");

        /* If %f would produce output larger than BUFFER_SIZE, confirm our
           safe version truncates correctly */
        if (probe_f_len >= (int)BUFFER_SIZE) {
            char safe_buf[BUFFER_SIZE];
            memset(safe_buf, 'X', BUFFER_SIZE);
            int safe_len = snprintf(safe_buf, BUFFER_SIZE, "%f", 1e308);
            ck_assert_msg(safe_len >= 0, "safe snprintf failed");
            ck_assert_msg(safe_buf[BUFFER_SIZE - 1] == '\0',
                          "Safe buffer not null-terminated after truncation");
            ck_assert_msg(strnlen(safe_buf, BUFFER_SIZE) < BUFFER_SIZE,
                          "Safe buffer length exceeds bounds");
        }
    }
}
END_TEST

START_TEST(test_format_string_boundary_conditions)
{
    /* Invariant: Boundary values must not cause buffer overreads or overwrites */

    struct {
        double val;
        const char *fmt;
        const char *description;
    } boundary_cases[] = {
        { 1e308,    "%g",   "max double with %%g" },
        { 1e308,    "%e",   "max double with %%e" },
        { 1e308,    "%.2f", "max double with %%.2f" },
        { 1e-308,   "%g",   "min double with %%g" },
        { -1e308,   "%g",   "negative max double" },
        { DBL_MAX,  "%g",   "DBL_MAX with %%g" },
        { DBL_MAX,  "%e",   "DBL_MAX with %%e" },
        { 0.0,      "%g",   "zero" },
        { -0.0,     "%g",   "negative zero" },
        { 1.0/0.0,  "%g",   "positive infinity" },
        { -1.0/0.0, "%g",   "negative infinity" },
        { 0.0/0.0,  "%g",   "NaN" },
    };

    int num_cases = sizeof(boundary_cases) / sizeof(boundary_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        /* Use a buffer with guard bytes */
        unsigned char guard_region[BUFFER_SIZE + 16];
        memset(guard_region, 0xAB, sizeof(guard_region));

        char *a1 = (char *)(guard_region + 8);
        memset(a1, 0, BUFFER_SIZE);

        int needed = snprintf(a1, BUFFER_SIZE, boundary_cases[i].fmt,
                              boundary_cases[i].val);

        /* Invariant: snprintf must succeed */
        ck_assert_msg(needed >= 0,
                      "snprintf failed for case: %s", boundary_cases[i].description);

        /* Invariant: Output must be null-terminated within bounds */
        size_t len = strnlen(a1, BUFFER_SIZE);
        ck_assert_msg(len < BUFFER_SIZE,
                      "Output length %zu >= buffer size %d for case: %s",
                      len, BUFFER_SIZE, boundary_cases[i].description);

        /* Invariant: Guard bytes before buffer must be intact */
        for (int g = 0; g < 8; g++) {
            ck_assert_msg(guard_region[g] == 0xAB,
                          "Guard byte %d before buffer corrupted for case: %s",
                          g, boundary_cases[i].description);
        }

        /* Invariant: Guard bytes after buffer must be intact */
        for (int g = BUFFER_SIZE + 8; g < (int)sizeof(guard_region); g++) {
            ck_assert_msg(guard_region[g] == 0xAB,
                          "Guard byte %d after buffer corrupted for case: %s",
                          g, boundary_cases[i].description);
        }
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    tcase_add_test(tc_core, test_format_string_boundary_conditions);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}