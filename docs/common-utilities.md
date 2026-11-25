
# Common Utilities

## PRNG

The Xorshift32 algorithm is used for pseudo-random
number generation with shift parameters 13, 17 and 5.

- Identical seeds produce identical sequences.
- A zero seed is mapped to 1.
- Uniform values are generated in the range [0,1).
- DATA and ACK directions can use separate PRNG states with struct.

Reference: https://en.wikipedia.org/wiki/Xorshift

## Time

The now_us() function uses CLOCK_MONOTONIC
and returns timestamps in microseconds.

## Logging

The existing CSV logging implementation is retained.

## Testing

PRNG and logging tests passed in the Linux Ubuntu 24.04 Docker environment.
