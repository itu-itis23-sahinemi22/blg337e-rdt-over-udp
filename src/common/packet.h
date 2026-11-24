/* Packet format shared by sender, receiver and emulator (docs/interfaces.md section 2, issue #6). */
#ifndef PACKET_H
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

/*
    header layout, big-endian:
    type u8 | flags u8 | len u16 | num u32 | crc32 u32 | payload (len bytes)
 */
#define HDR_LEN    12
#define OFF_TYPE    0
#define OFF_FLAGS   1
#define OFF_LEN     2
#define OFF_NUM     4
#define OFF_CRC     8

/* a UDP datagram carries at most 65507 bytes and the header takes 12 */
#define MAX_PAYLOAD 65495
#define MAX_PKT     (HDR_LEN + MAX_PAYLOAD)

#define TYPE_DATA   1
#define TYPE_ACK    2

#define FLAG_RETX   0x01 /* retransmission; for analysis only, the protocol never reads it */
#define FLAG_LAST   0x02 /* DATA packet that carries the end of the file */

/*
    big-endian access to the multi-byte fields.
    the only functions that know the byte order.
*/
uint16_t read_u16(const uint8_t *buffer, size_t off);
void     write_u16(uint8_t *buffer, size_t off, uint16_t v);
uint32_t read_u32(const uint8_t *buffer, size_t off);
void     write_u32(uint8_t *buffer, size_t off, uint32_t v);

/*
    writes a packet into buffer: header, payload, then the CRC over both.
    buffer must hold at least HDR_LEN + len bytes and must not overlap payload.
    payload may be NULL when len is 0.
    returns the packet size (HDR_LEN + len) to pass to sendto().
*/
size_t packet_build(uint8_t *buffer, uint8_t type, uint8_t flags, uint32_t num,
                    const uint8_t *payload, uint16_t len);

/* result of packet_check(): PKT_OK, or the first check that failed */
enum pkt_status {
    PKT_OK = 0,
    PKT_SHORT,    /* n < HDR_LEN */
    PKT_BAD_CRC,  /* CRC over the n bytes received does not match */
    PKT_BAD_TYPE, /* not the type this program expects */
    PKT_BAD_LEN,  /* len != n - HDR_LEN */
    PKT_BAD_NUM,  /* num >= 2^k */
};

/*
    checks the n bytes received into buffer in the order of docs/interfaces.md section 2.5
    and stops at the first failure.
    want_type is TYPE_DATA at the receiver and TYPE_ACK at the sender; k is 3..8.
    buffer is not const: the CRC field is set to 0 while the CRC is computed and then
    restored, so buffer is unchanged when the function returns.
 */
enum pkt_status packet_check(uint8_t *buffer, size_t n, uint8_t want_type, unsigned k);

/* short name of a status for log lines, e.g. "bad_crc" */
const char *packet_status_str(enum pkt_status s);

#endif
