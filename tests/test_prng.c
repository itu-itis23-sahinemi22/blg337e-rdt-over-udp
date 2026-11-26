#include "prng.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    prng_t a, b;

    prng_init(&a, 337);
    prng_init(&b, 337);

    for (int i = 0; i < 1000; i++) {
        assert(prng_next(&a) == prng_next(&b));
    }

    prng_init(&a, 337);
    prng_init(&b, 338);

    assert(prng_next(&a) != prng_next(&b));

    prng_init(&a, 337);
    assert(a.state != 337);

    prng_init(&b, 338);
    assert(b.state != 338);

    prng_init(&a, 0);
    assert(prng_next(&a) != 0);

    for (int i = 0; i < 1000; i++) {
        double value = prng_uniform(&a);
        assert(value >= 0.0 && value < 1.0);
    }

    for (int i = 0; i < 1000; i++) {
        assert(!prng_chance(&a, 0.0));
        assert(prng_chance(&a, 1.0));
    }

    prng_t data_rng, ack_rng;
    uint32_t seed = 337;

    prng_init(&data_rng, seed);
    prng_init(&ack_rng, seed ^ 0x9e3779b9U);

    assert(data_rng.state != ack_rng.state);

    for (int i = 0; i < 1000; i++) {
        assert(prng_next(&data_rng) != prng_next(&ack_rng));
    }
    printf("PRNG tests passed\n");

    return 0;
}
