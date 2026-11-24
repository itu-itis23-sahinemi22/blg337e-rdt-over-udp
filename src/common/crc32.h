/* CRC-32 (IEEE 802.3), used to detect corrupted packets (#4). */
#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
    CRC-32 of the len bytes at data.
    reflected poly 0xEDB88320, initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
    check value crc32("123456789", 9) == 0xCBF43926. empty input gives 0.
    data may be NULL when len is 0.
 */
uint32_t crc32(const uint8_t *data, size_t len);

/*
    same result as crc32(), computed one bit at a time without the table
    about 8 times slower, for reference for tests compare crc32() against.
 */
uint32_t crc32_bitwise(const uint8_t *data, size_t len);

#endif
