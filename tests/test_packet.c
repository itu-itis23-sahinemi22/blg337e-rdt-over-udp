/*
    unit tests for the packet format (issue #6).
    exit status 0 when every check passes, 1 otherwise.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "packet.h"

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
    fills buffer with a fixed pattern, so a payload copied to the wrong offset shows up as a mismatch.
 */
static void fill_pattern(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(i * 7 + 3);
}

/*
    recomputes the CRC after a test has changed a header field on purpose.
    without this, every such packet would stop at the CRC check and
    the checks after it (type, len, num) would never be reached.
*/
static void refresh_crc(uint8_t *buffer, size_t n)
{
    write_u32(buffer, OFF_CRC, 0);
    write_u32(buffer, OFF_CRC, crc32(buffer, n));
}

/*
    1. byte order: multi-byte fields are big-endian, whatever the host's byte order is,
    and each write touches only its own field.
*/
static void test_byte_order(void)
{
    uint8_t buffer[HDR_LEN] = {0};

    write_u16(buffer, OFF_LEN, 0xA1B2);
    check(buffer[2] == 0xA1 && buffer[3] == 0xB2, "write_u16(0xA1B2) gives bytes A1 B2");
    check(read_u16(buffer, OFF_LEN) == 0xA1B2, "read_u16 reads back 0xA1B2");

    write_u32(buffer, OFF_NUM, 0x01020304u);
    check(buffer[4] == 0x01 && buffer[5] == 0x02 && buffer[6] == 0x03 && buffer[7] == 0x04,
          "write_u32(0x01020304) gives bytes 01 02 03 04");
    check(read_u32(buffer, OFF_NUM) == 0x01020304u, "read_u32 reads back 0x01020304");

    check(buffer[OFF_TYPE] == 0 && buffer[OFF_FLAGS] == 0 && read_u32(buffer, OFF_CRC) == 0,
          "writes do not touch the neighbouring fields");

    //top byte >= 128: wrong if the byte is shifted as an int instead of a uint32_t
    write_u32(buffer, OFF_CRC, 0xC8000000u);
    check(read_u32(buffer, OFF_CRC) == 0xC8000000u, "read_u32 reads back 0xC8000000 (200 << 24)");
}

/*
    2. packet_build: every field lands at its offset, the payload follows the header,
    and the CRC is computed over the whole packet with the CRC field set to 0 (section 2.4).
*/
static void test_build(void)
{
    static uint8_t buffer[MAX_PKT], payload[1024], copy[MAX_PKT];
    fill_pattern(payload, sizeof(payload));

    size_t n = packet_build(buffer, TYPE_DATA, FLAG_LAST | FLAG_RETX, 5, payload, 1024);
    check(n == HDR_LEN + 1024, "packet_build returns HDR_LEN + len");
    check(buffer[OFF_TYPE] == TYPE_DATA && buffer[OFF_FLAGS] == (FLAG_LAST | FLAG_RETX) && read_u16(buffer, OFF_LEN) == 1024 && read_u32(buffer, OFF_NUM) == 5,
          "type, flags, len and num are at their offsets");
    check(memcmp(buffer + HDR_LEN, payload, 1024) == 0, "payload follows the header");

    /* recompute the CRC independently: on a copy, with the CRC field zeroed */
    memcpy(copy, buffer, n);
    write_u32(copy, OFF_CRC, 0);
    check(read_u32(buffer, OFF_CRC) == crc32(copy, n), "CRC covers the whole packet with the CRC field as 0");
}

/*
    3. a packet that packet_build made passes packet_check, and checking it leaves the buffer unchanged.
    also the two edge sizes: an ACK with no payload (NULL) and a packet with MAX_PAYLOAD bytes.
 */
static void test_round_trip(void)
{
    static uint8_t buffer[MAX_PKT], payload[MAX_PAYLOAD], copy[MAX_PKT];
    fill_pattern(payload, sizeof(payload));

    size_t n = packet_build(buffer, TYPE_DATA, 0, 5, payload, 1024);
    memcpy(copy, buffer, n);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_OK, "a built DATA packet passes packet_check");
    check(memcmp(buffer, copy, n) == 0, "packet_check leaves the buffer unchanged");

    n = packet_build(buffer, TYPE_ACK, 0, 7, NULL, 0);
    check(n == HDR_LEN && packet_check(buffer, n, TYPE_ACK, 3) == PKT_OK,
          "an ACK with no payload (NULL, 0) is HDR_LEN bytes and passes");

    n = packet_build(buffer, TYPE_DATA, 0, 255, payload, MAX_PAYLOAD);
    check(n == MAX_PKT && packet_check(buffer, n, TYPE_DATA, 8) == PKT_OK,
          "a packet with MAX_PAYLOAD bytes is MAX_PKT bytes and passes");
}

/*
    4. n < HDR_LEN: too short to hold a header.
 */
static void test_short(void)
{
    uint8_t buffer[HDR_LEN];
    size_t n = packet_build(buffer, TYPE_ACK, 0, 1, NULL, 0);

    check(packet_check(buffer, n - 1, TYPE_ACK, 3) == PKT_SHORT, "HDR_LEN - 1 bytes is short");
    check(packet_check(buffer, 0, TYPE_ACK, 3) == PKT_SHORT, "0 bytes is short");
}

/*
    5.CRC: every single-bit flip, and a datagram that lost its last byte, is bad_crc.
    the buffer is restored even when the check fails.
 */
static void test_bad_crc(void)
{
    static uint8_t buffer[MAX_PKT], payload[100], copy[MAX_PKT];
    fill_pattern(payload, sizeof(payload));
    size_t n = packet_build(buffer, TYPE_DATA, 0, 2, payload, 100);

    size_t missed = 0;
    for (size_t b = 0; b < n * 8; b++)
    {
        uint8_t mask = (uint8_t)(1u << (b % 8));

        buffer[b / 8] ^= mask; // flip bit b 
        if (packet_check(buffer, n, TYPE_DATA, 3) != PKT_BAD_CRC)
        {
            if (missed == 0)
                printf("  first flip not reported as bad_crc: bit %zu\n", b);
            missed++;
        }
        buffer[b / 8] ^= mask; // flip it back 
    }
    check(missed == 0, "every single-bit flip in a 112-byte packet is bad_crc");

    check(packet_check(buffer, n - 1, TYPE_DATA, 3) == PKT_BAD_CRC, "a packet missing its last byte is bad_crc");

    buffer[HDR_LEN] ^= 0x01;
    memcpy(copy, buffer, n);
    packet_check(buffer, n, TYPE_DATA, 3);
    check(memcmp(buffer, copy, n) == 0, "packet_check leaves the buffer unchanged when the CRC fails");
}

/*
    6. type: a packet of the wrong type, or of type 0, is bad_type.
    a corrupted packet of the wrong type is bad_crc, because the CRC is checked first.
 */
static void test_bad_type(void)
{
    static uint8_t buffer[MAX_PKT], payload[100];
    fill_pattern(payload, sizeof(payload));
    size_t n = packet_build(buffer, TYPE_DATA, 0, 2, payload, 100);

    check(packet_check(buffer, n, TYPE_ACK, 3) == PKT_BAD_TYPE, "a DATA packet where an ACK is expected is bad_type");

    buffer[OFF_TYPE] = 0;
    refresh_crc(buffer, n);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_BAD_TYPE, "type 0 is bad_type");

    buffer[HDR_LEN] ^= 0x01;
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_BAD_CRC, "a corrupted packet of the wrong type is bad_crc");
}

/*
    7. len must equal n - HDR_LEN. a len larger than the datagram is rejected
    without reading past it, because the CRC covers the n bytes received, not HDR_LEN + len.
 */
static void test_bad_len(void)
{
    static uint8_t buffer[MAX_PKT], payload[100];
    fill_pattern(payload, sizeof(payload));
    size_t n = packet_build(buffer, TYPE_DATA, 0, 2, payload, 100);

    write_u16(buffer, OFF_LEN, 99);
    refresh_crc(buffer, n);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_BAD_LEN, "len one less than the payload is bad_len");

    write_u16(buffer, OFF_LEN, 60000);
    refresh_crc(buffer, n);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_BAD_LEN, "len larger than the datagram is bad_len");
}

/*
    8. num must be below 2^k. the boundary values on both sides, and the same num with a larger k.
 */
static void test_bad_num(void)
{
    static uint8_t buffer[MAX_PKT], payload[100];
    fill_pattern(payload, sizeof(payload));

    size_t n = packet_build(buffer, TYPE_DATA, 0, 7, payload, 100);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_OK, "num 7 with k = 3 passes");

    n = packet_build(buffer, TYPE_DATA, 0, 8, payload, 100);
    check(packet_check(buffer, n, TYPE_DATA, 3) == PKT_BAD_NUM, "num 8 with k = 3 is bad_num");
    check(packet_check(buffer, n, TYPE_DATA, 4) == PKT_OK, "num 8 with k = 4 passes");

    n = packet_build(buffer, TYPE_DATA, 0, 0xFFFFFFFFu, payload, 100);
    check(packet_check(buffer, n, TYPE_DATA, 8) == PKT_BAD_NUM, "num 0xFFFFFFFF with k = 8 is bad_num");
}

/*
    9. packet_status_str gives each status its own name, and "unknown" for an invalid value.
 */
static void test_status_str(void)
{
    check(strcmp(packet_status_str(PKT_OK), "ok") == 0, "PKT_OK is \"ok\"");
    check(strcmp(packet_status_str(PKT_BAD_CRC), "bad_crc") == 0, "PKT_BAD_CRC is \"bad_crc\"");

    int distinct = 1;
    for (int a = PKT_OK; a <= PKT_BAD_NUM; a++)
        for (int b = a + 1; b <= PKT_BAD_NUM; b++)
            if (strcmp(packet_status_str((enum pkt_status)a), packet_status_str((enum pkt_status)b)) == 0)
                distinct = 0;
                
    check(distinct, "every status has a different name");

    check(strcmp(packet_status_str((enum pkt_status)42), "unknown") == 0, "an invalid status is \"unknown\"");
}

int main(void)
{
    puts("packet:");
    test_byte_order();
    test_build();
    test_round_trip();
    test_short();
    test_bad_crc();
    test_bad_type();
    test_bad_len();
    test_bad_num();
    test_status_str();

    if (failures > 0)
    {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    puts("all checks passed");
    return 0;
}
