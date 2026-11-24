# 2. Protocol design

This section summarizes the protocol and the reasons for its main design decisions. The protocol
specification gives every rule in full and contains the state-machine diagrams of the sender and
the receiver of each protocol: `docs/protocol.md`, which refers to `docs/interfaces.md` for the
byte-level details.

## 2.1 Principles

- **No connection set-up or tear-down.** The sender and the receiver are started with the same
  protocol, $W$ and $k$, and the receiver needs nothing from the sender except the data. The end of
  the transfer is a flag on the last DATA packet (section 2.6).
- **Two packet types.** The sender sends only DATA packets, the receiver only ACKs.
- **An invalid packet is treated exactly like a lost one.** It gets no reply, and the sender's
  retransmission timer recovers it. No header field is used before the CRC check has passed.
- **One event loop per program.** Each program is single-threaded and waits in `poll()`. All times
  are microseconds on `CLOCK_MONOTONIC`, the same clock in all three programs, so their logs can be
  merged.

## 2.2 Packet format

Every datagram is one packet: a 12-byte header followed by the payload.

| Offset | Size  | Field   | Content |
|-------:|------:|---------|----------------------------------------------------------------|
| 0      | 1     | `type`  | DATA = 1, ACK = 2. Every other value is invalid, 0 included, so a zeroed buffer is never a valid packet. |
| 1      | 1     | `flags` | `LAST` (0x02): the last DATA packet of the file. `RETX` (0x01): a retransmission. |
| 2      | 2     | `len`   | Payload length in bytes |
| 4      | 4     | `num`   | Sequence number (DATA) or acknowledgment number (ACK), below $2^k$ |
| 8      | 4     | `crc32` | CRC-32 over header and payload |
| 12     | `len` | payload | File data, in DATA packets only |

Multi-byte fields are big-endian, and packets are built and parsed byte by byte, never as a C
struct, so the format depends neither on compiler padding nor on the host's byte order.

`type`, `len`, `num` and `crc32` are the fields the assignment requires. The only addition is
`flags`, with two bits:

- `LAST` ends the transfer without a separate packet type. It is set on every transmission of the
  last DATA packet.
- `RETX` exists for the analysis only; the protocol never reads it. It lets the emulator's log tell
  new data from retransmitted data, which the link-utilization metric needs. Sequence numbers cannot
  tell them apart, because they wrap.

`num` is 32 bits wide for every $k$, and only its low $k$ bits are used. A fixed 32-bit field keeps
every field aligned to its own size and the format the same for every $k$; compared with an 8-bit
field it costs 3 bytes per packet, 0.3 % at a 1024-byte payload. `len` is 16 bits wide, and the
maximum payload is 65495 bytes: the largest UDP payload, 65507 bytes, minus the header.

**Error detection.** The CRC-32 is that of IEEE 802.3 (reflected polynomial `0xEDB88320`, initial
value and final XOR `0xFFFFFFFF`), implemented by us. It covers the header and the payload, computed
with the `crc32` field set to 0. UDP's own checksum cannot replace it: the emulator flips a bit and
then sends a new datagram, whose UDP checksum is computed over the corrupted bytes. Since the
emulator flips one bit, and a CRC detects every single-bit error, every corrupted packet is detected.

**Validation.** Each received datagram goes through five checks, in this order, and the first one
that fails drops it:

1. it is at least 12 bytes long;
2. the CRC matches, computed over the bytes received;
3. `type` is the one this program expects: DATA at the receiver, ACK at the sender;
4. `len` equals the received size minus 12;
5. `num` is below $2^k$.

The CRC comes before any check of a header field, so a flipped bit in `len` can never make a program
read past its buffer.

## 2.3 Sequence numbers and window limits

Sequence numbers are counted modulo $2^k$, with $3 \le k \le 8$. The sender counts packets with
indices $i = 0, 1, 2, \ldots$ that never wrap, and writes $i \bmod 2^k$ into packet $i$. Both ends
compare numbers by their distance $d(x, y) = (x - y) \bmod 2^k$.

The protocols are correct only for $W \le 2^k - 1$ (Go-Back-N) and $W \le 2^{k-1}$ (Selective
Repeat). Both limits come from the same case: every packet of a window arrives, every ACK is lost,
and the sender retransmits the oldest packet while the receiver already waits for new ones. The
Go-Back-N receiver must not take that packet for the one it expects next, $W$ packets later. The
Selective Repeat receiver must tell old packets (up to $W$ below its window) from new ones (inside
its window), which takes $2W$ distinct numbers. Experiment 5 (section 9) derives these limits and
shows the failures above them. Larger windows are therefore accepted, with a warning.

## 2.4 The three ARQ protocols

|                                      | Stop-and-Wait | Go-Back-N | Selective Repeat |
|--------------------------------------|---------------|-----------|------------------|
| Packets outstanding                  | 1             | up to $W$ | up to $W$ |
| ACK `num` means                      | "I have packet `num`" | "I have every packet before `num`" (cumulative) | "I have packet `num`" (individual) |
| Receiver, packet not next in order   | discard, ACK `num` | discard, ACK the expected number again | inside the window: store, ACK `num`; up to $W$ below it: discard, ACK `num`; elsewhere: discard, no ACK |
| Retransmission timers                | one           | one, for the oldest unacknowledged packet | one per outstanding packet |
| On a timeout, resend                 | the packet    | every outstanding packet | only the packet whose timer expired |
| Safe window                          | 1             | $2^k - 1$ | $2^{k-1}$ |

Two rules hold for all three protocols:

- **A duplicate or old packet is discarded but acknowledged again.** It usually arrives because the
  sender never got the earlier ACK; without a new one, the sender would retransmit until it gives up.
- **An ACK that acknowledges nothing new is ignored and restarts no timer.** Otherwise a stream of
  repeated ACKs could postpone a retransmission forever.

Selective Repeat does not keep $W$ separate timers. All timers last the same RTO, so the one that
expires first belongs to the unacknowledged packet sent longest ago, and the event loop waits for
that one only.

## 2.5 Retransmission timeout

The RTO is estimated from RTT samples with the estimator of RFC 6298, Karn's rule and exponential
backoff. One estimator serves every timer of the sender. With an RTT sample $R$:

$$
\mathit{RTTVAR} \leftarrow (1 - \beta)\,\mathit{RTTVAR} + \beta\,\lvert \mathit{SRTT} - R \rvert,
\qquad
\mathit{SRTT} \leftarrow (1 - \alpha)\,\mathit{SRTT} + \alpha R
$$

$$
\mathit{RTO} = \min\bigl(\mathit{RTO}_{\max},\ \mathit{SRTT} + \max(\mathit{RTO}_{\min},\ K \cdot \mathit{RTTVAR})\bigr)
$$

The first sample sets $\mathit{SRTT} = R$ and $\mathit{RTTVAR} = R/2$; before it, the RTO is 1 s.
Every timeout doubles the RTO, up to $\mathit{RTO}_{\max}$, and the next sample computes it again
from $\mathit{SRTT}$ and $\mathit{RTTVAR}$, which undoes the backoff. All values are integer
microseconds.

| Parameter | Value | Reason |
|-----------|-------|---------------------------------------------------------------------|
| $\alpha$, $\beta$, $K$ | 1/8, 1/4, 4 | The values of RFC 6298, so that Experiment 3 shows how the standard estimator follows the RTT. |
| Initial RTO | 1 s | RFC 6298. It is longer than the RTT of the first packet in every experiment, which is at most about 450 ms (200 ms delay each way, with 10 % jitter). |
| $\mathit{RTO}_{\min}$ | 200 ms (`-r`) | Linux's minimum RTO. RFC 6298 asks for 1 s, a conservative value for the Internet, where a receiver may delay its ACKs by up to 500 ms; ours acknowledges every packet at once. At the baseline, where the RTT of Stop-and-Wait is about 48 ms, a loss then costs an RTO of about 250 ms, 5 RTTs, instead of about 20 RTTs. |
| Margin above $\mathit{SRTT}$ | $\max(\mathit{RTO}_{\min}, K \cdot \mathit{RTTVAR})$ | RFC 6298 uses the clock granularity $G$ (1 ms) in place of $\mathit{RTO}_{\min}$. Without jitter every RTT is the same and $\mathit{RTTVAR}$ falls to 0, so the RTO would be only 1 ms above the RTT once the queue makes the RTT longer than $\mathit{RTO}_{\min}$ (about 265 ms with $W = 32$ at the baseline). An ACK delayed by a few milliseconds of scheduling would then cause a needless retransmission. Like Linux, we keep $\mathit{RTO}_{\min}$ as the least margin. |
| $\mathit{RTO}_{\max}$ | 2 s | RFC 6298 allows a maximum only from 60 s, for paths whose RTT nobody knows. Here the emulator sets the RTT, and the largest one in our experiments is about 1 s: Selective Repeat with $W = 64$ at 200 ms delay, whose first window waits in the queue. 2 s stays above it, and it bounds how long a retransmitting sender can stay silent, which sets the receiver's linger time (section 2.6). |
| Backoff | $\times 2$ per timeout | RFC 6298. Under Karn's rule a retransmitted packet gives no sample, so if the RTT has grown beyond the RTO, only the backoff can raise it. |

**RTT samples (Karn's rule).** The ACK of a retransmitted packet cannot tell which transmission it
answers, so only a packet sent exactly once gives a sample, timed from its transmission to its ACK.
Stop-and-Wait samples each packet's ACK. Go-Back-N samples the newest packet that a cumulative ACK
acknowledges, because that packet's arrival made the receiver send the ACK. Selective Repeat samples
every individual ACK.

**Selective Repeat timers use the current RTO.** A running timer expires one RTO after its packet
was sent, with the RTO as it is now, not as it was at the time of sending. At the start of a
transfer with a large window, the emulator's queue fills up and each packet's RTT is longer than the
one before; the samples of the earlier packets raise the RTO before the later timers expire. A
timer fixed at send time would resend packets for nothing: with $W = 64$ at the baseline, packet 64
is sent with an RTO of 248 ms, computed from a 48 ms RTT, but its own RTT is about 530 ms. The
cost of this choice is discussed in section 10.

## 2.6 Termination

- The DATA packet that carries the end of the file has `LAST` set. An empty file is one DATA packet
  with `len = 0`, and a file whose size is a multiple of the maximum payload needs no extra empty
  packet.
- The **sender** is done when every packet, the `LAST` one included, is acknowledged, and exits with
  status 0. This is exactly where the completion-time metric ends. If one packet is still not
  acknowledged after `-x` retransmissions (10 by default), the sender gives up and exits with
  status 1.
- The **receiver's** file is complete when the `LAST` packet has been delivered in order. The
  receiver then lingers: it keeps acknowledging DATA packets, in case its final ACK was lost, until
  no datagram has arrived for `-q` milliseconds. It then exits with status 0 if the file is complete
  and 1 if it is not, which means the sender has given up.
- `-q` must outlast the longest silence of a sender that is still retransmitting: `-x` intervals of
  at most $\mathit{RTO}_{\max}$, plus one RTT. With the defaults this is
  $10 \times 2\,\text{s} + 2\,\text{s} = 22\,\text{s}$, so the default `-q` is 25 s.

TFTP (RFC 1350) ends a transfer in a similar way, with a final short block and a wait for the last
ACK. An explicit flag avoids the extra empty block that TFTP needs when the file size is a multiple
of the block size.
