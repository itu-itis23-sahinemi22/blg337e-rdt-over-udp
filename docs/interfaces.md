# Interfaces

This document fixes the interfaces between the three programs: the packet format they exchange,
what acknowledgments mean, how a transfer ends, and how each program is started.
Sections 1–5 (sender and receiver) belong to #1, sections 6–7 (logs, emulator, scenarios) to #2.
Any change to this file goes through an issue that both of us approve (see CONTRIBUTING.md).

## 1. Overview

Three separate processes on `127.0.0.1` communicate only through UDP datagrams.
The sender and receiver never talk to each other directly: every datagram passes through the emulator.

```
            DATA                       DATA
  sender ─────────▶ :E  emulator  ─────────────▶ :R  receiver
         ◀─────────               ◀─────────────
             ACK                        ACK
```

- The **sender** knows only the emulator's port `E` and sends every DATA packet there.
  Its own port is chosen by the operating system.
- The **receiver** knows only its own port `R`. It sends each ACK to the source address of the
  datagram it is answering, as returned by `recvfrom()`. That address always belongs to the emulator.
- The **emulator** is configured with `E` and `R`. It learns the sender's address from the first
  datagram that arrives on `E`, and forwards ACKs there.

Data flows only from the sender to the receiver. The receiver sends only ACKs.

## 2. Packet format

Every datagram is one packet: a fixed 12-byte header followed by the payload.

### 2.1 Byte layout

| Offset | Size | Field     | Meaning                                                        |
|-------:|-----:|-----------|----------------------------------------------------------------|
| 0      | 1    | `type`    | Packet type                                                    |
| 1      | 1    | `flags`   | Bit flags                                                      |
| 2      | 2    | `len`     | Payload length in bytes                                        |
| 4      | 4    | `num`     | Sequence number (DATA) or acknowledgment number (ACK)          |
| 8      | 4    | `crc32`   | CRC-32 over header and payload                                 |
| 12     | `len`| payload   | File data (DATA only)                                          |

```
byte:   0      1      2    3     4    5    6    7     8    9    10   11    12 ...
      [type][flags][   len   ][        num          ][       crc32         ][payload...]
```

`type`, `num`, `len` and `crc32` are the fields the assignment requires. The only extra field is
`flags`; section 2.3 gives the reason for each of its bits.

The largest possible packet is `MAX_PKT` = 12 + 65495 = 65507 bytes (see `len` in 2.3). Every
program that receives packets, the emulator included, uses a receive buffer of at least `MAX_PKT`
bytes, so that no datagram is ever truncated.

### 2.2 Byte order and helpers

Multi-byte fields are **big-endian** (network byte order). A packet is built and parsed byte by
byte in a `uint8_t` buffer; no C struct is ever sent on the wire, so the layout does not depend on
compiler padding or on the host's byte order.

`src/common/packet.h` names every offset, so no code uses a bare index:

```c
#define HDR_LEN    12
#define OFF_TYPE    0
#define OFF_FLAGS   1
#define OFF_LEN     2
#define OFF_NUM     4
#define OFF_CRC     8

#define MAX_PAYLOAD 65495
#define MAX_PKT     (HDR_LEN + MAX_PAYLOAD)

#define TYPE_DATA   1
#define TYPE_ACK    2
#define FLAG_RETX   0x01
#define FLAG_LAST   0x02
```

and provides the only functions that know the byte order:

```c
uint16_t read_u16 (const uint8_t *buf, size_t off);
void     write_u16(uint8_t *buf, size_t off, uint16_t v);
uint32_t read_u32 (const uint8_t *buf, size_t off);
void     write_u32(uint8_t *buf, size_t off, uint32_t v);
```

All three programs, the emulator included, use these names and helpers; none of them defines its
own offsets.

### 2.3 Fields

**`type`**

| Value | Name   | Sent by  |
|------:|--------|----------|
| 1     | `DATA` | sender   |
| 2     | `ACK`  | receiver |

Every other value, including 0, is invalid and the packet is dropped. 0 is deliberately unused so
that a zeroed buffer is never a valid packet.

There are only these two types: the end of a transfer is marked by a flag, not by a packet type
(section 4). So every packet from the sender to the receiver is a DATA packet, and the one carrying
`LAST` is an ordinary DATA packet in every other respect: it goes through the same checks and is
subject to the same impairments in the emulator (data loss, corruption, duplication, reordering).

**`flags`**

| Bit | Mask   | Name   | Meaning |
|----:|--------|--------|---------|
| 0   | `0x01` | `RETX` | Set by the sender when this DATA packet is a retransmission. **For analysis only:** it lets the emulator's log tell new data from retransmitted data, which the link-utilization metric needs; sequence numbers cannot, because they wrap modulo 2^k. The protocol never reads this bit; in particular the receiver must not use it to decide whether a packet is new. |
| 1   | `0x02` | `LAST` | Set on the DATA packet that carries the end of the file, including every retransmission of it. See section 4. |
| 2–7 | —      | —      | Reserved: sent as 0, ignored on receipt. |

ACK packets always carry `flags = 0`.

**`len`**: number of payload bytes. In DATA packets `0 ≤ len ≤` max payload (`len = 0` only for the
single packet of an empty file). In ACK packets `len = 0`. Although the field is 16 bits wide, the
max payload is limited to 65495 bytes, because a UDP datagram carries at most 65507 bytes and the
header takes 12.

**`num`**: the sequence number in DATA packets, the acknowledgment number in ACK packets (its
meaning per protocol is in section 3). Only the values `0 … 2^k − 1` are used: the sender writes the
sequence number modulo 2^k, and a packet with `num ≥ 2^k` is invalid. The field is 32 bits wide for
every `k` (3 ≤ k ≤ 8); `k` only limits how many of its bits are used. It never carries a counter
wider than `k` bits, otherwise the sequence-number limits of Experiment 5 could not appear.
A 32-bit field keeps every header field aligned to its own size and the header at 12 bytes; compared
with an 8-bit field it costs 3 bytes per packet (0.3 % at a 1024-byte payload).

### 2.4 CRC-32

- **Algorithm:** the CRC-32 of IEEE 802.3, implemented by us (no library): reflected polynomial
  `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`.
  Check value: `crc32("123456789") = 0xCBF43926`.
- **Coverage:** the whole datagram, header and payload, with the `crc32` field set to 0 while
  computing.
- **Sending:** build the packet with `crc32 = 0`, compute the CRC over all `HDR_LEN + len` bytes,
  write the result at `OFF_CRC`.
- **Receiving:** save the stored value, set the field to 0, compute the CRC over the `n` bytes
  actually received, compare.

UDP's own checksum cannot be relied on: the emulator flips a bit after receiving a datagram and then
sends a new one, whose UDP checksum is computed over the already corrupted bytes and is therefore valid.

### 2.5 Validation order

Every received datagram (DATA at the receiver, ACK at the sender) is checked in this order.
The first failed check drops the packet, and the drop is logged with its reason.

1. `n ≥ HDR_LEN`, where `n` is the size returned by `recvfrom()`. Otherwise it cannot hold a header.
2. The CRC matches, computed over the `n` bytes received. **No header field is trusted before this
   check:** a flipped bit in `len` must never make us read past the buffer.
3. `type` is the one this program expects: DATA at the receiver, ACK at the sender.
4. `len == n − HDR_LEN`.
5. `num < 2^k`.

A dropped packet is treated exactly like a lost one: nothing is sent in reply to it, and recovery is
left to the sender's retransmission timer.

## 3. Acknowledgments

The receiver answers DATA packets with ACK packets (`type = ACK`, `flags = 0`, `len = 0`).
What the acknowledgment number `num` means depends on the protocol, which both ends get from `-p`:

| Protocol         | `num` in an ACK                         | Meaning                                                       |
|------------------|-----------------------------------------|---------------------------------------------------------------|
| Stop-and-Wait    | sequence number of the packet received  | "I have packet `num`."                                        |
| Go-Back-N        | next sequence number the receiver expects | "I have every packet before `num`; send `num` next." (cumulative) |
| Selective Repeat | sequence number of the packet received  | "I have packet `num`." (individual)                           |

Sequence numbers wrap modulo 2^k, so comparisons use the distance `d(x, y) = (x − y) mod 2^k`.

### 3.1 When the receiver sends which ACK

These rules apply to DATA packets that passed every check of section 2.5. The common principle:
**a duplicate or old packet is discarded but still acknowledged**, because its arrival usually means
the sender never got our earlier ACK.

**Stop-and-Wait.** The receiver expects sequence number `e` (initially 0).
- `num == e`: deliver the payload, `e ← e + 1`, send ACK `num`.
- otherwise: discard, send ACK `num`.

**Go-Back-N.** The receiver expects sequence number `e` (initially 0).
- `num == e`: deliver the payload, `e ← e + 1`, send ACK `e` (the new expected number).
- otherwise (out of order, duplicate or old): discard, send ACK `e` again.

So if packet 0 is lost and packet 1 arrives first, the receiver answers ACK 0: no special case at
the start.

**Selective Repeat.** The receive window starts at `rb` (initially 0) and holds `W` numbers.
- In the window, `d(num, rb) < W`: store the packet unless it is already stored, send ACK `num`.
  If `num == rb`, deliver it and every consecutive stored packet after it, and move `rb` past them.
- Just below the window, `1 ≤ d(rb, num) ≤ W` (already delivered): discard, send ACK `num`.
- Anywhere else: discard, send nothing.

### 3.2 How the sender reads an ACK

- **Stop-and-Wait:** an ACK whose `num` equals the outstanding packet's sequence number completes
  it; any other ACK is ignored.
- **Go-Back-N:** the sender counts packets with indices that never wrap: `base` is the oldest
  unacknowledged packet, `next` the next packet to send, so `next − base` packets are outstanding.
  Only the number written into a packet is taken modulo 2^k. An ACK with
  `1 ≤ d(num, base mod 2^k) ≤ next − base` acknowledges that many packets, from `base` up to the
  one before `num`, and `base` moves past them. Any other ACK (including a repeat of the current
  `base`) acknowledges nothing new and is ignored.
  Counting in packets matters at `W = 2^k`: with a full window `next mod 2^k = base mod 2^k`,
  so a bound of `d(next, base)` would be 0 and every ACK would be ignored.
- **Selective Repeat:** an ACK whose `num` is an outstanding, not yet acknowledged packet marks that
  packet acknowledged; the window then slides past every acknowledged packet at its start.
  Any other ACK is ignored.

These rules are only unambiguous while `W ≤ 2^k − 1` (Go-Back-N) and `W ≤ 2^(k−1)`
(Selective Repeat). Larger windows are allowed on purpose (section 5) so that Experiment 5 can show
what goes wrong.

## 4. Termination

A transfer ends with the `LAST` flag. There are no separate connection set-up or tear-down packets.

### 4.1 Sender

- The DATA packet that carries the last byte of the file has `LAST` set, on its first transmission
  and on every retransmission.
- The sender is finished when every DATA packet, including the `LAST` one, is acknowledged. It logs
  completion and exits with status 0. This is exactly the instant at which the completion-time
  metric ends.
- **Giving up:** if one packet has been retransmitted `-x` times and is still not acknowledged,
  the sender logs the failure and exits with status 1. Without this limit the sender would retry
  forever once the receiver has exited (see the last row of the table in 4.3).

### 4.2 Receiver

- Before the first datagram arrives, the receiver waits indefinitely, since the sender may be
  started later (5.4).
- The file is complete when the `LAST` packet is **delivered in order**, that is, once every packet
  before it has been delivered too. With Selective Repeat the `LAST` packet may arrive early; it then
  waits in the buffer like any other packet. On completion the receiver closes the output file and
  logs it.
- **Lingering:** the receiver does not exit immediately. It keeps answering DATA packets by the rules
  of section 3, so that if its final ACK was lost, the sender's retransmission is acknowledged again.
- **Linger time `-q`:** from the first datagram on, the receiver exits once `-q` milliseconds pass
  without any datagram arriving; every arriving datagram, valid or not, restarts this period.
  - If the file is complete, it exits with status 0. This is the normal end of a transfer.
  - If it is not, the sender has given up (4.1). The receiver logs the failure and exits with
    status 1; the incomplete output file is left as it is. Without this rule it would wait forever.
- `-q` must be longer than the longest silence a sender that has not given up can cause: the
  intervals between one packet's transmissions up to its `-x`-th retransmission added up (each one
  the RTO after backoff), plus one RTT as a margin. Otherwise the receiver can leave while the
  sender is still retransmitting: a retransmitted `LAST` packet then finds no receiver, or a
  transfer that was still running ends with status 1.
  The RTO is at most 2 s (5.1), so each interval is at most 2 s and the RTT is below 2 s whenever
  the protocol works at all. With the default `-x 10` the silence stays under 10 × 2 s + 2 s = 22 s,
  which the default `-q` of 25 s outlasts. A run with a larger `-x` needs a larger `-q`.

### 4.3 Edge cases

| Case                                         | What happens                                                         |
|----------------------------------------------|----------------------------------------------------------------------|
| File size is a multiple of the max payload   | The last packet is full and carries `LAST`; no extra empty packet.   |
| Empty file                                   | One DATA packet with `len = 0` and `LAST`.                           |
| `LAST` arrives before earlier packets (SR)   | Buffered; the file is complete only when it is delivered in order.   |
| A bit flip hits `LAST` (or any header bit)   | The CRC check fails and the packet is dropped like a lost one.       |
| The final ACK is lost                        | The sender retransmits; the lingering receiver acknowledges again.   |
| The sender gives up before the file is complete | After `-q` ms of silence the receiver exits with status 1.        |
| No datagram ever reaches the receiver        | It waits indefinitely (4.2); the run scripts stop it.                |
| The receiver has already exited              | No ACK ever comes; the sender gives up after `-x` retransmissions.   |

### 4.4 Design note

No connection set-up is needed because both ends are started with the same `-p`, `-w` and `-k`,
and the receiver needs nothing from the sender except the data. Ending on a flag in the last DATA
packet keeps the data phase identical in all three protocols and makes the protocol finish exactly
when the completion-time measurement does. TFTP (RFC 1350) ends a transfer in a similar way, with a
final short block and a waiting period for the last ACK; an explicit flag avoids the extra empty
block that TFTP needs when the file size is a multiple of the block size.

## 5. Sender and receiver command line

Both programs take short options parsed with POSIX `getopt()`. Every parameter can be changed at run
time; nothing needs recompiling. The defaults are the baseline of the assignment, so a run that sets
only the required options is a baseline run.

### 5.1 Options

| Option | Sender                                   | Receiver                    | Default       |
|--------|------------------------------------------|-----------------------------|---------------|
| `-p`   | Protocol: `sw`, `gbn` or `sr`            | Same                        | required      |
| `-w`   | Window size `W`                          | Same                        | 16            |
| `-k`   | Sequence-number width in bits            | Same                        | 7             |
| `-m`   | Max payload in bytes                     | —                           | 1024          |
| `-f`   | File to send                             | File to write               | required      |
| `-P`   | Emulator port `E` to send to             | Port `R` to listen on       | required      |
| `-L`   | Log file                                 | Log file                    | required      |
| `-r`   | Minimum RTO in ms, also its least margin above the smoothed RTT | —    | 200           |
| `-x`   | Max retransmissions of one packet before giving up | —                 | 10            |
| `-q`   | —                                        | Linger time in ms (4.2)     | 25000         |
| `-h`   | Print usage and exit                     | Same                        | —             |

`-p`, `-w` and `-k` must be the same on both ends. The protocol does not check this; the run
scripts do.

The sender estimates its retransmission timeout from RTT samples as in RFC 6298 (#13), with
`RTO = SRTT + max(-r, 4 · RTTVAR)`: like Linux, it uses the minimum in place of the clock
granularity, so the RTO always stays at least `-r` above the smoothed RTT. The RTO starts at 1 s,
is at most 2 s, and doubles on every timeout until the next sample. Only packets that were never
retransmitted give samples (Karn's rule). The protocol specification (#24) gives the reasons for
these values. The 2 s maximum is what sets the default of `-q` (4.2).

### 5.2 Allowed values

| Option | Allowed values | Notes |
|--------|----------------|-------|
| `-p`   | `sw`, `gbn`, `sr` | |
| `-w`   | `1 ≤ W ≤ 2^k`  | Ignored with `-p sw`, where the window is always 1. Above the safe limit (`2^k − 1` for `gbn`, `2^(k−1)` for `sr`) the program prints a warning, logs it, and **still runs**, so that Experiment 5 can show the failure. |
| `-k`   | `3 ≤ k ≤ 8`    | |
| `-m`   | `1 … 65495`    | 65495 = largest UDP payload (65507) minus the 12-byte header. |
| `-P`   | `1 … 65535`    | |
| `-r`, `-q` | `1 … 2147483647` | 2147483647 = 2^31 − 1 = `INT_MAX` ms, about 24.8 days: `poll()` takes its timeout as an `int`. |
| `-x`   | `1 … 4294967295` | 4294967295 = 2^32 − 1, the largest `unsigned`. |

A value outside these ranges, an unknown option or a missing required option prints the usage and
exits with status 2.

### 5.3 Exit status

| Status | Meaning |
|-------:|---------|
| 0      | Success. Sender: every packet acknowledged. Receiver: file complete and linger time over. |
| 1      | The transfer failed. Sender: gave up after `-x` retransmissions of one packet (4.1). Receiver: `-q` ms of silence before the file was complete (4.2). |
| 2      | Invalid arguments, a set-up error such as an unreadable input file or a socket that cannot be opened, or a system error during the run, such as an output or log file that cannot be written. |

### 5.4 Example

A baseline Go-Back-N run. Start the receiver first, then the emulator, then the sender, so that the
first DATA packet finds everything ready:

```sh
./bin/receiver -p gbn -w 16 -k 7 -P 9002 -f received.bin -L runs/scratch/receiver.log
./bin/emulator ...                                     # options in section 7
./bin/sender   -p gbn -w 16 -k 7 -P 9001 -f test.bin   -L runs/scratch/sender.log
```

Since `-w 16 -k 7` are the defaults, the same run can be started with
`./bin/sender -p gbn -P 9001 -f test.bin -L runs/scratch/sender.log`.
