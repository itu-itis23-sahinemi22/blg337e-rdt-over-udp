/* CRC-32 (IEEE 802.3): bitwise reference and table-driven version (issue #4). */
#include "crc32.h"

#define CRC32_POLY   0xEDB88320u /* 0x04C11DB7 with its bits reversed */
#define CRC32_INIT   0xFFFFFFFFu
#define CRC32_XOROUT 0xFFFFFFFFu

/*
    runs the shift register for 8 rounds: shifts the 8 low bits of crc out
    to the right and XORs in the polynomial whenever a 1 falls out.
    crc must be unsigned so that >> always shifts in zeros.
 */
static uint32_t shift_8_bits(uint32_t crc)
{
    for (int i = 0; i < 8; i++) {
        if (crc & 1u)
            crc = (crc >> 1) ^ CRC32_POLY;
        else
            crc >>= 1;
    }
    return crc;
}

uint32_t crc32_bitwise(const uint8_t *data, size_t len)
{
    uint32_t crc = CRC32_INIT;

    for (size_t i = 0; i < len; i++)
        crc = shift_8_bits(crc ^ data[i]);

    return crc ^ CRC32_XOROUT;
}

/*
    crc_table[i] is the result of the 8 rounds when the low byte of the register is i.
    built on the first call to crc32() so callers never need an init function.
 */
static uint32_t crc_table[256];
static int table_ready = 0;

static void build_table(void)
{
    for (uint32_t i = 0; i < 256; i++)
        crc_table[i] = shift_8_bits(i);
    table_ready = 1;
}

uint32_t crc32(const uint8_t *data, size_t len)
{
    if (!table_ready)
        build_table();

    uint32_t crc = CRC32_INIT;

    /* the 8 rounds depend only on the low byte of (crc ^ data[i]), the other 24 bits just move down by 8. */
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);

    return crc ^ CRC32_XOROUT;
}
