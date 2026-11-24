/* Packet format: byte-order helpers, building and checking packets (issue #6). */
#include "packet.h"

#include <string.h>

#include "crc32.h"

uint16_t read_u16(const uint8_t *buffer, size_t off)
{
    return (uint16_t)(((unsigned)buffer[off] << 8) | buffer[off + 1]);
}

void write_u16(uint8_t *buffer, size_t off, uint16_t v)
{
    buffer[off] = (uint8_t)(v >> 8);
    buffer[off + 1] = (uint8_t)v;
}

uint32_t read_u32(const uint8_t *buffer, size_t off)
{
    // cast before shifting: buffer[off] is promoted to int, and 200 << 24 overflows an int
    return ((uint32_t)buffer[off] << 24) | ((uint32_t)buffer[off + 1] << 16) 
    | ((uint32_t)buffer[off + 2] << 8) | (uint32_t)buffer[off + 3];
}

void write_u32(uint8_t *buffer, size_t off, uint32_t v)
{
    buffer[off] = (uint8_t)(v >> 24);
    buffer[off + 1] = (uint8_t)(v >> 16);
    buffer[off + 2] = (uint8_t)(v >> 8);
    buffer[off + 3] = (uint8_t)v;
}

size_t packet_build(uint8_t *buffer, uint8_t type, uint8_t flags, uint32_t num,
                    const uint8_t *payload, uint16_t len)
{
    buffer[OFF_TYPE] = type;
    buffer[OFF_FLAGS] = flags;
    write_u16(buffer, OFF_LEN, len);
    write_u32(buffer, OFF_NUM, num);
    write_u32(buffer, OFF_CRC, 0);

    // memcpy with a NULL pointer is undefined even for 0 bytes
    if (len > 0)
        memcpy(buffer + HDR_LEN, payload, len);

    size_t n = HDR_LEN + (size_t)len;
    write_u32(buffer, OFF_CRC, crc32(buffer, n));
    return n;
}

enum pkt_status packet_check(uint8_t *buffer, size_t n, uint8_t want_type, unsigned k)
{
    if (n < HDR_LEN)
        return PKT_SHORT;

    /*
        no header field is trusted before this check. the CRC covers the n bytes
        actually received, not HDR_LEN + len: a flipped bit in len must never
        make us read past the buffer.
    */
    uint32_t stored = read_u32(buffer, OFF_CRC);
    write_u32(buffer, OFF_CRC, 0);
    uint32_t computed = crc32(buffer, n);
    write_u32(buffer, OFF_CRC, stored);
    if (computed != stored)
        return PKT_BAD_CRC;

    if (buffer[OFF_TYPE] != want_type)
        return PKT_BAD_TYPE;
    if ((size_t)read_u16(buffer, OFF_LEN) != n - HDR_LEN)
        return PKT_BAD_LEN;
    if (read_u32(buffer, OFF_NUM) >= (1u << k))
        return PKT_BAD_NUM;
    return PKT_OK;
}

const char *packet_status_str(enum pkt_status s)
{
    // no default: with -Wall the compiler warns if a status is missing here
    switch (s)
    {
    case PKT_OK:
        return "ok";
    case PKT_SHORT:
        return "short";
    case PKT_BAD_CRC:
        return "bad_crc";
    case PKT_BAD_TYPE:
        return "bad_type";
    case PKT_BAD_LEN:
        return "bad_len";
    case PKT_BAD_NUM:
        return "bad_num";
    }
    return "unknown";
}
