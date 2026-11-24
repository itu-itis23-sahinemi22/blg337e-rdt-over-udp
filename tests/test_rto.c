/*
    unit tests for the RTO estimator (issue #13).
    exit status 0 when every check passes, 1 otherwise.
 */
#include <stdint.h>
#include <stdio.h>

#include "rto.h"

static int failures = 0;

/*
    prints the result of one check and counts it if it failed.
 */
static void check(int ok, const char *what)
{
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

#define MS 1000u /* microseconds */

/* limits wide enough that no test value is clamped unless a test is about clamping */
#define NO_MIN 1u
#define NO_MAX (UINT64_MAX / 4)

/*
    1. before any sample the RTO is 1 s (RFC 6298 2.1), moved into [min, max].
 */
static void test_init(void)
{
    struct rto e;

    rto_init(&e, 200 * MS, 1000 * MS);
    check(e.rto_us == 1000 * MS && !e.has_sample, "min 200 ms, max 1 s: starts at 1 s with no sample");
    rto_init(&e, 200 * MS, 500 * MS);
    check(e.rto_us == 500 * MS, "max 500 ms: starts at 500 ms");
    rto_init(&e, 2000 * MS, 1000 * MS);
    check(e.rto_us == 2000 * MS && e.max_us == 2000 * MS, "min 2 s above max 1 s: max is raised, starts at 2 s");
}

/*
    2. the first two samples, worked out by hand from RFC 6298 2.2 and 2.3.
 */
static void test_first_samples(void)
{
    struct rto e;
    rto_init(&e, NO_MIN, NO_MAX);

    // (2.2) SRTT = R, RTTVAR = R/2, RTO = 100 + 4 * 50 = 300 ms
    rto_sample(&e, 100 * MS);
    check(e.has_sample && e.srtt_us == 100 * MS && e.rttvar_us == 50 * MS && e.rto_us == 300 * MS,
          "first sample 100 ms: srtt 100, rttvar 50, rto 300 ms");

    // (2.3) RTTVAR = 3/4 * 50 + 1/4 * |100 - 200| = 62.5, SRTT = 7/8 * 100 + 1/8 * 200 = 112.5,
    // RTO = 112.5 + 4 * 62.5 = 362.5 ms
    rto_sample(&e, 200 * MS);
    check(e.rttvar_us == 62500 && e.srtt_us == 112500 && e.rto_us == 362500,
          "second sample 200 ms: rttvar 62.5, srtt 112.5, rto 362.5 ms");
}

/*
    3. a long run of samples against the same formulas in double precision, with the
    200 ms minimum. integer division drops less than 1 us per step, and the averages forget
    old errors, so the two stay within 0.1 ms, far below the 1 ms of the timers.
 */
static void test_against_double(void)
{
    struct rto e;
    rto_init(&e, 200 * MS, NO_MAX);

    double srtt = 0, rttvar = 0, rto = 0, worst = 0;
    uint32_t x = 12345; // a fixed LCG, so every run checks the same sequence
    for (int i = 0; i < 10000; i++) {
        x = x * 1103515245u + 12345u;
        uint64_t r = 20 * MS + (x >> 8) % (600 * MS); // 20 .. 620 ms

        rto_sample(&e, r);
        if (i == 0) {
            srtt = (double)r;
            rttvar = (double)r / 2;
        } else {
            double diff = srtt > (double)r ? srtt - (double)r : (double)r - srtt;
            rttvar = 0.75 * rttvar + 0.25 * diff;
            srtt = 0.875 * srtt + 0.125 * (double)r;
        }
        rto = srtt + (4 * rttvar > 200 * MS ? 4 * rttvar : 200 * MS);

        double err = (double)e.rto_us - rto;
        if (err < 0)
            err = -err;
        if (err > worst)
            worst = err;
    }
    printf("        (largest difference: %.1f us)\n", worst);
    check(worst < 100, "10000 random samples: within 0.1 ms of the double-precision RTO");
}

/*
    4. the limits: the minimum (-r) is the least margin above SRTT, and the maximum caps the RTO.
 */
static void test_limits(void)
{
    struct rto e;

    // a constant RTT drives RTTVAR to 0, and the minimum takes the place of 4 * RTTVAR
    rto_init(&e, 200 * MS, 1000 * MS);
    for (int i = 0; i < 100; i++)
        rto_sample(&e, 48 * MS);
    check(e.srtt_us == 48 * MS && e.rttvar_us == 0 && e.rto_us == 248 * MS,
          "constant 48 ms RTT (SW at the baseline), min 200 ms: rttvar 0, rto 248 ms");

    // the case the margin is for: an RTT above the minimum, with the RFC's G it would be 266 ms
    rto_init(&e, 200 * MS, 1000 * MS);
    for (int i = 0; i < 100; i++)
        rto_sample(&e, 265 * MS);
    check(e.rto_us == 465 * MS, "constant 265 ms RTT (W = 32), min 200 ms: rto 465 ms, not 266 ms");

    // a variance term above the minimum is used as it is: 400 + 4 * 200 = 1200 ms
    rto_init(&e, 200 * MS, NO_MAX);
    rto_sample(&e, 400 * MS);
    check(e.rto_us == 1200 * MS, "first sample 400 ms, min 200 ms: 4 * rttvar = 800 ms wins, rto 1.2 s");

    rto_init(&e, 200 * MS, 1000 * MS);
    rto_sample(&e, 5000 * MS);
    check(e.rto_us == 1000 * MS, "a 5 s sample with max 1 s: rto 1 s");
}

/*
    5. backoff (RFC 6298 5.5): doubles up to the maximum, and the next sample undoes it.
 */
static void test_backoff(void)
{
    struct rto e;
    rto_init(&e, NO_MIN, 1000 * MS);
    rto_sample(&e, 100 * MS); // rto 300 ms

    rto_backoff(&e);
    check(e.rto_us == 600 * MS, "backoff: 300 -> 600 ms");
    rto_backoff(&e);
    check(e.rto_us == 1000 * MS, "backoff: 600 ms -> 1 s, not 1.2 s (max)");
    rto_backoff(&e);
    check(e.rto_us == 1000 * MS, "backoff at the max stays at 1 s");

    // srtt 100, rttvar 3/4 * 50 = 37.5: rto 100 + 150 = 250 ms, computed again from the averages
    rto_sample(&e, 100 * MS);
    check(e.rto_us == 250 * MS, "a new sample after backoff: rto 250 ms, the backoff is gone");

    // no overflow even with no maximum at all
    rto_init(&e, NO_MIN, UINT64_MAX);
    int monotonic = 1;
    for (int i = 0; i < 70; i++) {
        uint64_t before = e.rto_us;
        rto_backoff(&e);
        if (e.rto_us < before)
            monotonic = 0;
    }
    check(monotonic && e.rto_us == UINT64_MAX, "70 backoffs with max UINT64_MAX: never wraps, ends at the max");
}

/*
    6. tracking: the RTO follows a sudden jump in the RTT at once. beta = 1/4 and K = 4 add the
    whole jump to K * RTTVAR, while SRTT moves only 1/8 of the way.
 */
static void test_tracking(void)
{
    struct rto e;
    rto_init(&e, 200 * MS, NO_MAX);
    for (int i = 0; i < 100; i++)
        rto_sample(&e, 50 * MS); // srtt 50 ms, rttvar 0, rto 250 ms

    // RTTVAR = 1/4 * 350 = 87.5, SRTT = 7/8 * 50 + 1/8 * 400 = 93.75, RTO = 93.75 + 350 = 443.75 ms
    rto_sample(&e, 400 * MS);
    check(e.rto_us == 443750, "50 ms then one 400 ms sample: rto 443.75 ms, already above the new RTT");

    int above = 1;
    for (int i = 0; i < 100; i++) {
        rto_sample(&e, 400 * MS);
        if (e.rto_us <= 400 * MS)
            above = 0;
    }
    check(above && e.srtt_us > 399 * MS && e.rto_us == e.srtt_us + 200 * MS,
          "100 more samples of 400 ms: srtt reaches 400 ms, rto never drops below it and ends 200 ms above");
}

int main(void)
{
    puts("rto:");
    test_init();
    test_first_samples();
    test_against_double();
    test_limits();
    test_backoff();
    test_tracking();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
