/* 
    unit tests for CRC-32 (issue #4). 
    exit status 0 when every check passes, 1 otherwise.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"

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

/*
    1. standard check value: crc32() and crc32_bitwise() must both give
    0xCBF43926 for the 9 ASCII bytes "123456789".
 */
static void test_check_value(void)
{
    const char input[] = "123456789";
    size_t len = strlen(input);

    uint32_t received_crc = crc32((const uint8_t *)input, len);
    check(received_crc == 0xCBF43926u, "crc32(\"123456789\") == 0xCBF43926u");

    received_crc = crc32_bitwise((const uint8_t *)input, len);
    check(received_crc == 0xCBF43926u, "crc32_bitwise(\"123456789\") == 0xCBF43926u");

}

/*
    2. empty input: both functions must return 0 for (NULL, 0).
 */
static void test_empty(void)
{
    uint32_t received_crc = crc32(NULL, 0);
    check(received_crc == 0, "crc32(NULL) == 0");

    received_crc = crc32_bitwise(NULL, 0);
    check(received_crc == 0, "crc32_bitwise(NULL) == 0");
}

/*
    fills buffer with random values
*/
static void fill_random(uint8_t *buf, size_t len, unsigned seed)
{
    srand(seed);
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(rand() & 0xFF);
}

/*
    3. table vs reference: on the same pseudo-random buffer, crc32() and
    crc32_bitwise() must agree for every length from 0 to the buffer size.
    fixed seed, so a failure can be reproduced.
 */
static void test_table_matches_bitwise(void)
{
    uint8_t buffer[4096];
    fill_random(buffer, sizeof(buffer), 337);
    int ok = 1;
    for(size_t i = 0; i <= 4096; i++){
        if(crc32(buffer, i) != crc32_bitwise(buffer,i)){
            printf("n: %zu\n", i);
            ok = 0;
            break;
        }
    }
    check(ok, "crc32(buf, n) == crc32_bitwise(buf, n) for n 0..4096");
}

#define SAMPLE_LEN 1036 /* HDR_LEN + 1024 once packet.h exists */

/*
    4. every single-bit error is detected: on a 1036-byte sample packet
    (12-byte header + 1024-byte payload), flip each of the 8288 bits in turn.
    the CRC must differ from the original every time. flip the bit back after each try.
 */
static void test_single_bit_flips(void)
{
    uint8_t packet[SAMPLE_LEN];
    fill_random(packet, sizeof(packet), 4);
    uint32_t original = crc32(packet, sizeof(packet));

    size_t missed = 0;
    for (size_t b = 0; b < sizeof (packet) * 8; b++) {
        uint8_t mask = (uint8_t)(1u << (b % 8));

        packet[b / 8] ^= mask; /* flip bit b */
        if (crc32(packet, sizeof (packet)) == original) {
            if (missed == 0)
                printf("  first undetected flip: bit %zu\n", b);
            missed++;
        }
        packet[b / 8] ^= mask; /* flip it back */
    }

    check(missed == 0, "all 8288 single-bit flips in a 1036-byte packet are detected");
    check(crc32(packet, sizeof packet) == original, "packet is unchanged after the flips");
}

int main(void)
{
    puts("crc32:");
    test_check_value();
    test_empty();
    test_table_matches_bitwise();
    test_single_bit_flips();

    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
