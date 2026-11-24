/* Retransmission timeout of the sender: RFC 6298 estimator with exponential backoff (issue #13). */
#ifndef RTO_H
#define RTO_H

#include <stdint.h>

/* RFC 6298 (2.1): the RTO before the first RTT sample */
#define RTO_INITIAL_US 1000000u

/*
    upper limit of the RTO, in ms. RFC 6298 (2.5) asks for at least 60 s, for paths whose RTT
    nobody knows. here the emulator sets the RTT: the largest one in the experiments is about
    1 s. SR with W = 64 at 200 ms delay sends its whole first window at once, and the last of
    those packets waits 530 ms in the queue (64 x 1036 bytes at 1 Mbit/s) on top of the 408 ms
    RTT and up to 40 ms of jitter. 2 s stays above it plus the 200 ms margin of rto_sample(),
    and it bounds how long a sender that has not given up can stay silent: -x retransmissions,
    each at most 2 s apart. the receiver's -q must outlast that (section 4.2).
 */
#define RTO_MAX_MS 2000

/* state of one estimator; all times in microseconds */
struct rto {
    uint64_t srtt_us;   /* smoothed RTT */
    uint64_t rttvar_us; /* RTT variation */
    uint64_t rto_us;    /* current timeout, always within [min_us, max_us] */
    uint64_t min_us;    /* minimum RTO, and the least margin the RTO keeps above srtt (-r) */
    uint64_t max_us;
    int has_sample;     /* 0 until the first sample: srtt and rttvar are not set yet */
};

/*
    starts at RTO_INITIAL_US, moved into [min_us, max_us].
    a max_us below min_us is raised to min_us, so the RTO is then fixed at min_us.
 */
void rto_init(struct rto *e, uint64_t min_us, uint64_t max_us);

/*
    one RTT measurement (RFC 6298 2.2 and 2.3) and the new RTO:
    SRTT + max(min_us, 4 * RTTVAR), at most max_us. this also undoes any backoff.
    Karn's rule is the caller's job: only a packet that was never retransmitted may give a
    sample, since the ACK of a retransmitted one cannot tell which transmission it answers.
 */
void rto_sample(struct rto *e, uint64_t rtt_us);

/* a timer expired (RFC 6298 5.5): doubles the RTO, at most to max_us, until the next sample */
void rto_backoff(struct rto *e);

#endif
