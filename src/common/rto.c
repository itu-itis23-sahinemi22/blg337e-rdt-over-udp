/* RFC 6298 RTO estimator with exponential backoff (issue #13). */
#include "rto.h"

/*
    RFC 6298 parameters: alpha = 1/8, beta = 1/4, K = 4. alpha and beta appear as the
    integer fractions in rto_sample(), so no floating point is needed.
 */
#define RTO_K 4

static uint64_t clamp(uint64_t v, uint64_t lo, uint64_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

void rto_init(struct rto *e, uint64_t min_us, uint64_t max_us)
{
    e->srtt_us = 0;
    e->rttvar_us = 0;
    e->has_sample = 0;
    e->min_us = min_us;
    e->max_us = max_us < min_us ? min_us : max_us;
    e->rto_us = clamp(RTO_INITIAL_US, e->min_us, e->max_us);
}

void rto_sample(struct rto *e, uint64_t rtt_us)
{
    if (!e->has_sample) { // (2.2)
        e->srtt_us = rtt_us;
        e->rttvar_us = rtt_us / 2;
        e->has_sample = 1;
    } else { // (2.3): RTTVAR first, because it uses the SRTT before this sample
        uint64_t diff = e->srtt_us > rtt_us ? e->srtt_us - rtt_us : rtt_us - e->srtt_us;
        e->rttvar_us = (3 * e->rttvar_us + diff) / 4; // (1 - beta) * RTTVAR + beta * |SRTT - R'|
        e->srtt_us = (7 * e->srtt_us + rtt_us) / 8;   // (1 - alpha) * SRTT + alpha * R'
    }

    /*
        RFC 6298: RTO = SRTT + max(G, K * RTTVAR), with G the clock granularity (1 ms for poll()).
        like Linux, we use the minimum RTO in place of G, so the RTO stays at least min_us
        above SRTT, not just above min_us. without jitter every RTT is the same and RTTVAR
        falls to 0. with W = 32 the queue makes the RTT 265 ms, above the minimum, and with G
        the RTO would be only 1 ms longer than the RTT: an SR packet whose ACK is 2 ms late
        would be resent for nothing.
     */
    uint64_t var = RTO_K * e->rttvar_us;
    e->rto_us = clamp(e->srtt_us + (var > e->min_us ? var : e->min_us), e->min_us, e->max_us);
}

void rto_backoff(struct rto *e)
{
    // compared with max / 2 instead of doubling first, so a huge RTO cannot overflow
    e->rto_us = e->rto_us > e->max_us / 2 ? e->max_us : 2 * e->rto_us;
}
