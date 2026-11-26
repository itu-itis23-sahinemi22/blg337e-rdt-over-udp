
# Common Utilities

## PRNG

The Xorshift32 algorithm is used for pseudo-random
number generation with shift parameters 13, 17 and 5.

- Identical seeds produce identical sequences.
- A zero mixed seed is mapped to 1.
- Uniform values are generated in the range [0,1).
- DATA and ACK directions can use separate PRNG states with struct.

Reference: https://en.wikipedia.org/wiki/Xorshift

## Seed mixing

The MurmurHash3 32-bit finalizer is applied to the seed
before initializing the Xorshift32 state.

This reduces correlations between the initial outputs
generated from small or nearby seeds.

Seed mixing is performed only during initialization.
The Xorshift32 generation algorithm remains unchanged.

The same seed produces the same sequence of random numbers,
preserving reproducibility.

If the mixed seed is zero, the state is initialized to 1
to prevent the Xorshift32 zero-state problem.

Reference:
https://en.wikipedia.org/wiki/MurmurHash


## Direction-specific PRNG states

DATA and ACK use separate PRNG states.

The DATA seed is the configured seed.
The ACK seed is derived using seed XOR 0x9e3779b9.

Both seeds are mixed during initialization.
This avoids identical random number sequences in
the two directions while preserving reproducibility.


## Time

The now_us() function uses CLOCK_MONOTONIC
and returns timestamps in microseconds.

## Logging

The existing CSV logging implementation is retained.

## Testing

PRNG and logging tests passed in the Linux Ubuntu 24.04 Docker environment.
