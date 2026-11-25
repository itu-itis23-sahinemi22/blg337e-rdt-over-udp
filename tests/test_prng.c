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

    printf("PRNG tests passed\n");

    return 0;
}
