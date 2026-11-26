#ifndef PRNG_H
#define PRNG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t state;
} prng_t;

void prng_init(prng_t *rng, uint32_t seed);
uint32_t prng_next(prng_t *rng);
double prng_uniform(prng_t *rng);
bool prng_chance(prng_t *rng, double probability);

#endif
