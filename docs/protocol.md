# Protocol specification

This document specifies the reliable data transfer protocol that the sender and receiver run over
UDP: why the packet has the fields it has, how Stop-and-Wait, Go-Back-N and Selective Repeat
behave, how the retransmission timeout (RTO) is estimated, and why its parameters have their values.

The byte-level contract between the programs is in [interfaces.md](interfaces.md) and is part of
this specification. It is not repeated here; this document refers to it as "interfaces §N":

| interfaces | Contents |
|------------|----------|
| §1 | The three processes and who sends what to whom |
| §2 | Packet layout, byte order, CRC-32, validation order |
| §3 | What an ACK means, the receiver's rules, how the sender reads an ACK |
| §4 | Termination and its edge cases |
| §5 | Command line, allowed values, exit status |

The state-machine diagrams are in section 9.

## 1. Model

- The sender, the emulator and the receiver are three processes on `127.0.0.1`. Every DATA packet
  goes from the sender through the emulator to the receiver, and every ACK comes back the same way
  (interfaces §1).
- The emulator may lose, corrupt, duplicate, reorder and delay packets in both directions. The
  protocol assumes nothing about delivery; it relies only on the CRC-32 check, which catches every
  corrupted packet because the emulator flips one bit and a CRC detects every single-bit error.
- Data flows only from the sender to the receiver. There is no connection set-up: both ends are
  started with the same protocol, `W` and `k` (interfaces §4.4).
- Each program is single-threaded and runs one `poll()` loop. Times are microseconds on
  `CLOCK_MONOTONIC`, the same clock in all three programs, so their logs can be merged.

## 2. Packet format: the reason for each field

The layout is in interfaces §2.1: a 12-byte header, big-endian, followed by the payload.

| Field   | Size    | Why it exists | Why this size |
|---------|---------|---------------|---------------|
| `type`  | 1 byte  | Required by the assignment: tells DATA from ACK, so each program accepts only the type it expects (interfaces §2.5). | Two values are used. 0 is invalid, so a zeroed buffer is never a valid packet. |
| `flags` | 1 byte  | The only field beyond the required ones. `LAST` ends the transfer without a separate packet type (section 7). `RETX` marks retransmissions in the emulator's log, which the link-utilization metric needs; sequence numbers cannot tell, because they wrap. The protocol itself never reads `RETX`. | One byte holds both bits; the other six are reserved. |
| `len`   | 2 bytes | Required. Payloads differ in length: the last packet is usually shorter, and an empty file is one packet with `len = 0`. `len` must also equal the received size minus 12, a second check after the CRC. | 16 bits cover the largest UDP payload; the max payload is 65495 bytes so that header and payload fit in one UDP datagram. |
| `num`   | 4 bytes | Required: the sequence number in DATA packets, the acknowledgment number in ACK packets. Only values below `2^k` are used. | 32 bits keep every field aligned to its own size and the format the same for every `k`. The cost over 8 bits is 3 bytes per packet, 0.3 % at a 1024-byte payload. |
| `crc32` | 4 bytes | Required: detects corruption. UDP's checksum cannot, because the emulator flips the bit before it sends a new datagram, whose checksum is then computed over the corrupted bytes. | CRC-32 of IEEE 802.3, over header and payload, so a flipped bit in `num` or in `LAST` is caught as well as one in the data. |

There are only two packet types. The receiver never sends anything but ACKs, an ACK carries no
payload and `flags = 0`, and the end of the transfer is a flag on the last DATA packet.

## 3. Sequence numbers and windows

The sender counts packets with indices `0, 1, 2, …` that never wrap. Only the number written into a
packet is taken modulo `2^k`: packet `i` carries `num = i mod 2^k`. The receiver keeps only numbers
modulo `2^k`, and both ends compare numbers by the distance `d(x, y) = (x − y) mod 2^k`
(interfaces §3).

At most `W` packets are outstanding (sent but not acknowledged): `W = 1` for Stop-and-Wait, `-w` for
Go-Back-N and Selective Repeat. `W` may be anything from 1 to `2^k`, but the protocol is only correct
up to the limits below.

### 3.1 Go-Back-N: W ≤ 2^k − 1

Let `base` be the oldest unacknowledged packet. The sender can transmit any packet from `base` to
`base + W − 1`. The receiver expects some packet `e` with `base ≤ e ≤ base + W`: `e = base` if none
of them has arrived, `e = base + W` if all of them have arrived but every ACK was lost. The receiver
must never take a packet `i ≠ e` for packet `e`, so `i mod 2^k ≠ e mod 2^k` must hold. Since
`|i − e| ≤ W`, this holds for every such pair exactly when `W < 2^k`.

With `W = 2^k`: all `W` packets arrive, every ACK is lost, the sender times out and resends `base`,
and the receiver, which expects `base + 2^k`, delivers the old packet as new data.

### 3.2 Selective Repeat: W ≤ 2^(k−1)

The receive window starts at `rb` with `base ≤ rb ≤ base + W`. A packet the sender can transmit is
either in the receive window (`rb … rb + W − 1`, to be stored) or already delivered
(`rb − W … rb − 1`, to be acknowledged again). These are `2W` consecutive indices, and the receiver
can tell the two cases apart only if they have different numbers modulo `2^k`: `2W ≤ 2^k`.

With `W > 2^(k−1)`: all `W` packets arrive, every ACK is lost, the receive window moves past them,
and the sender's retransmission of `base` carries a number that is now also inside the receive
window. The receiver stores it as new data (`sr_on_data()` checks "inside the window" first).

### 3.3 Larger windows

A window above these limits is accepted on purpose, with a warning on `stderr` and in the log
(interfaces §5.2), so that Experiment 5 (#23) can show both failures. A transfer with such a window
can end with status 0 and still deliver a wrong file; only the SHA-256 comparison shows it.

## 4. Sender

### 4.1 Common to all three protocols

- The file is read into memory, so any packet can be resent: packet `i` is the bytes
  `[i·m, i·m + len)`, with `m` the max payload. There are `⌈size / m⌉` packets, at least one.
- Packet `i` is sent with `num = i mod 2^k`. The last packet has `LAST` on every transmission;
  every retransmission has `RETX`. A failed `sendto()` is not fatal: the timer runs as if the packet
  was lost.
- The loop waits in `poll()` until an ACK arrives or the earliest timer expires. A received datagram
  that fails the checks of interfaces §2.5 is dropped and logged, exactly as if it was lost.
- A timer runs for the current RTO (section 5) from the moment it was started. When it expires:
  1. if the packet it belongs to has already been retransmitted `-x` times, the sender gives up and
     exits with status 1 (interfaces §4.1);
  2. otherwise it retransmits (what depends on the protocol), doubles the RTO (5.2) and restarts the
     timer.
- An ACK that acknowledges nothing new is logged as ignored and changes nothing; in particular it
  does not restart a timer. Otherwise a stream of repeated ACKs could postpone a retransmission
  forever.

### 4.2 Overview

|                 | Stop-and-Wait | Go-Back-N | Selective Repeat |
|-----------------|---------------|-----------|------------------|
| Outstanding     | 1 packet      | up to `W` | up to `W` |
| An ACK acknowledges | the packet it names | every packet before the number it carries (cumulative) | the packet it names (individual) |
| Timers          | one, for the outstanding packet | one, for `base` | one per outstanding packet |
| On a timeout    | resend that packet | resend every outstanding packet | resend only the packet whose timer expired |
| RTT sample      | the packet, if sent once | the newest packet the ACK covers, if sent once | the packet, if sent once |

### 4.3 Stop-and-Wait (`sw_run()`)

1. Send packet `i` and start the timer.
2. An ACK with `num = i mod 2^k` completes packet `i`; go to packet `i + 1`. Any other ACK is ignored.
3. On a timeout, resend packet `i` and restart the timer.

### 4.4 Go-Back-N (`gbn_run()`)

1. **Fill the window:** send new packets while fewer than `W` are outstanding. If the window was
   empty, the timer starts with the first of them.
2. **ACK:** an ACK that acknowledges `n ≥ 1` packets (interfaces §3.2) moves `base` forward by `n`.
   If packets are still outstanding, the timer restarts for the new `base`. The window then has room
   for `n` new packets (step 1).
3. **Timeout:** resend every outstanding packet, `base` to `next − 1`, double the RTO once, and
   restart the timer. The give-up count is that of `base`: `base` was sent first and every timeout
   resends all outstanding packets, so no packet has been retransmitted more often.

### 4.5 Selective Repeat (`sr_run()`)

1. **Fill the window** as in Go-Back-N. Each packet's timer starts with its transmission.
2. **ACK:** an ACK for an outstanding, unacknowledged packet marks it. `base` then slides past every
   acknowledged packet at the start of the window, which makes room for new packets.
3. **Timeout:** resend only the packet whose timer expired, double the RTO, and restart that
   packet's timer.

**Timers.** The program does not keep `W` separate timers. All timers last the same RTO, so the one
that expires first belongs to the unacknowledged packet whose last transmission is the oldest; the
loop waits for that one only.

**Every timer uses the RTO current at the moment it is checked**, not the RTO at the moment it was
started. This matters while the emulator's queue fills up at the start of a transfer with a large
`W`: each packet waits longer in the queue than the one before, so its RTT is longer, and the
samples of the earlier packets raise the RTO before the later timers expire. A timer fixed at send
time would keep an RTO computed from the short early RTTs. For example, with `W = 64` at the
baseline, packet 64 is sent when the first ACK arrives, with an RTO of 248 ms computed from a 48 ms
RTT, but it waits behind a full window, so its own RTT is about 530 ms: it would be resent for
nothing.

The same rule has a cost: the timeout of one packet doubles the RTO for every running timer. If two
packets of one window are lost, the timeout of the first one also lengthens the timer of the second,
so the second is resent later. The delay ends when the next RTT sample (5.3) undoes the backoff or
when the doubled RTO runs out, whichever comes first.

## 5. Retransmission timeout

A fixed timeout is not allowed: the RTO is estimated from RTT samples. The estimator is RFC 6298
with Karn's rule and exponential backoff (`src/common/rto.c`). One estimator serves every timer of
the sender.

### 5.1 Estimator

Before the first sample, `RTO = 1 s`. With an RTT sample `R`:

```
first sample:   SRTT   = R
                RTTVAR = R / 2

later samples:  RTTVAR = (1 − β) · RTTVAR + β · |SRTT − R|     (with the SRTT from before this sample)
                SRTT   = (1 − α) · SRTT   + α · R

then:           RTO    = min(RTO_max, SRTT + max(RTO_min, K · RTTVAR))
```

On every timeout: `RTO = min(2 · RTO, RTO_max)`. The next sample computes the RTO from `SRTT` and
`RTTVAR` again, which undoes the backoff.

All values are integer microseconds. `α` and `β` are applied as `7/8` and `3/4` of the old value
plus `1/8` and `1/4` of the new one, so no floating point is needed. Timers wait in `poll()`, which
takes milliseconds; the time left is rounded up, so a timer never fires early.

### 5.2 Parameters and their reasons

| Parameter | Value | Reason |
|-----------|-------|--------|
| `α`, `β`, `K` | 1/8, 1/4, 4 | The values of RFC 6298. Keeping the standard constants lets Experiment 3 compare our RTO with the standard estimator's, and 1/8 and 1/4 are exact in integer arithmetic. |
| Initial RTO | 1 s | RFC 6298 (2.1). It is longer than the RTT of the first packet in every experiment: at most about 450 ms, at 200 ms propagation delay with 20 ms jitter each way. So the first packet is never resent before its ACK could arrive. |
| `RTO_min` | `-r`, default 200 ms | Linux's minimum RTO. RFC 6298 (2.4) asks for 1 s as a conservative choice for the Internet, where a TCP receiver may also delay its ACKs by up to 500 ms (RFC 1122). Here the path is the emulator and the receiver acknowledges every packet at once. At the baseline (about 48 ms RTT with Stop-and-Wait), a loss costs an RTO of about 250 ms, 5 RTTs; with a 1 s minimum it would cost about 20 RTTs and dominate Experiment 2. 200 ms is still far above the few milliseconds of scheduling delay that three processes on one machine can add. |
| Margin above `SRTT` | `max(RTO_min, K · RTTVAR)` | RFC 6298 uses `max(G, K · RTTVAR)`, with `G` the clock granularity (1 ms with `poll()`). We use `RTO_min` in place of `G`, as Linux does. Without jitter every RTT is the same, and `RTTVAR` shrinks to 0 (it is multiplied by 3/4 at every sample). The RFC formula then gives `RTO = SRTT + 1 ms`. With `W = 32` at the baseline, the queue makes the RTT about 265 ms, which is above `RTO_min` itself, so the minimum would not help: an ACK 2 ms late would trigger a needless retransmission. With our margin the RTO stays at least `RTO_min` above `SRTT`. |
| `RTO_max` | 2 s | RFC 6298 (2.5) allows a maximum only if it is at least 60 s, for paths whose RTT nobody knows. Here the emulator sets the RTT, and the largest one in the experiments is about 1 s: Selective Repeat with `W = 64` at 200 ms delay sends its whole first window at once, and its last packet waits 522 ms in the queue behind the other 63 (63 × 1036 bytes at 1 Mbit/s), on top of the 408 ms RTT and up to 40 ms of jitter: about 970 ms. 2 s stays above that plus the 200 ms margin. It also bounds how long a sender that has not given up can stay silent, which sets the receiver's linger time `-q` (interfaces §4.2): with a 60 s maximum, `-q` would have to be at least ten minutes. |
| Backoff | × 2 per timeout, up to `RTO_max` | RFC 6298 (5.5). If the RTT has suddenly grown, the doubled RTO soon exceeds it. Karn's rule (5.3) means retransmitted packets give no samples, so without the backoff the RTO would never catch up. |
| Retransmission limit | `-x`, default 10 | Not part of RFC 6298: it ends a transfer whose receiver is gone (interfaces §4.1). |

### 5.3 RTT samples of retransmitted packets (Karn's rule)

An ACK of a retransmitted packet cannot tell which transmission it answers. Timed from the last
transmission, the sample is too short if the ACK answers an earlier one; timed from the first, it is
too long if the ACK answers the last one. So **only a packet that was sent exactly once gives an RTT
sample**, timed from that transmission to the arrival of its ACK. A retransmitted packet gives none,
and the backed-off RTO stays until the next sample.

- **Stop-and-Wait:** the ACK of packet `i` is a sample if `i` was sent once.
- **Go-Back-N:** an ACK that moves `base` gives one sample: for the newest packet it acknowledges
  (the one just before the new `base`), if that packet was sent once. The receiver sends ACK `e`
  when packet `e − 1` arrives in order, so the ACK answers the newest packet it covers. An ACK that
  covers several packets means the ACKs before it were lost; timing it from an older packet would
  add the time that packet waited. After a timeout every outstanding packet has been resent, so
  samples resume only with packets sent for the first time after it.
  *Limitation:* the receiver also repeats ACK `e` when a later packet arrives out of order. If the
  first ACK `e` is lost and packet `e` is lost or delayed too, the first ACK `e` the sender sees is a
  repeat sent when a later packet arrived. The sample of packet `e − 1` is then too long by the time
  between the two arrivals. This needs two impairments at once and only makes the RTO larger.
- **Selective Repeat:** every ACK that marks a packet is a sample if that packet was sent once. Since
  each packet has its own ACK, Selective Repeat gets the most samples.

Every sample is logged as an `rtt_sample` event and every change of the RTO as an `rto_update` event
(the log format is #2). Experiment 3 plots both.

## 6. Receiver

The receiver's rules are in interfaces §3.1. In short:

| | Expected packet | Other packet in the window (SR) | Packet just below the window | Anything else |
|---|---|---|---|---|
| **Stop-and-Wait** | deliver, ACK `num` | — | discard, ACK `num` | discard, ACK `num` |
| **Go-Back-N** | deliver, ACK the new expected number | — | discard, ACK the expected number | discard, ACK the expected number |
| **Selective Repeat** | deliver it and every stored packet after it, ACK `num` | store unless already stored, ACK `num` | discard, ACK `num` | discard, no ACK |

- **A duplicate is discarded but still acknowledged:** it usually arrives because the sender never
  got the earlier ACK.
- **Selective Repeat ignores packets outside both ranges:** every packet the sender may still send
  is either inside the receive window or at most `W` below its start (section 3.2), so such a packet
  cannot be one the sender still waits for (with `W ≤ 2^(k−1)`).
- A packet that fails the checks of interfaces §2.5 gets no reply; the sender's timer recovers it.
- Each ACK goes to the source address of the DATA packet it answers, which is the emulator.
- Selective Repeat keeps a copy of each stored payload in a buffer indexed by sequence number
  (`2^k ≤ 256` entries) until it can be delivered in order.
- Once `LAST` has been delivered, the file is closed and nothing is delivered or stored again, but
  DATA packets are still acknowledged, so that a retransmission caused by a lost final ACK is
  answered (section 7).

## 7. Termination

Termination is specified in interfaces §4; the design choice is explained in interfaces §4.4.

- The last DATA packet carries `LAST`. There are no set-up or tear-down packets.
- The sender is done when every packet, the `LAST` one included, is acknowledged. This is also where
  the completion-time metric ends. It gives up after `-x` retransmissions of one packet.
- The receiver's file is complete when the `LAST` packet is delivered in order. The receiver keeps
  answering until no datagram has arrived for `-q` ms, then exits: with status 0 if the file is
  complete, with 1 if it is not.

## 8. How each impairment is handled

| Impairment | Stop-and-Wait | Go-Back-N | Selective Repeat |
|------------|---------------|-----------|------------------|
| DATA loss | Timeout, the packet is resent. | The packets after it arrive out of order and are discarded; each is answered with the ACK of the lost one, which the sender ignores. Timeout, the whole window is resent. | The packets after it are stored and acknowledged. Only the lost one times out and is resent. |
| ACK loss | Timeout, the packet is resent; the receiver discards the duplicate and acknowledges it again. | The next cumulative ACK covers it. Only if no later ACK arrives before the timer expires is the window resent. | That packet times out and is resent; the receiver discards the duplicate and acknowledges it again. |
| Corruption | The CRC check fails and the packet is dropped: the same as losing that DATA packet or ACK. | Same | Same |
| Duplication | The second copy of a DATA packet is discarded and acknowledged again. A repeated ACK acknowledges nothing new and is ignored. | Same | Same |
| Reordering | Only one packet is in flight in each direction, so a held-back packet waits for the next packet in its direction, which normally comes only after a timeout. The late copy is then a duplicate. | A DATA packet overtaken by the next one: the next one is discarded, so it is effectively lost, and the window is resent after a timeout. Reordered ACKs cost nothing: a later cumulative ACK covers the earlier one. | The overtaking packet is stored and delivered as soon as the held-back one arrives. Nothing is resent. |

## 9. State-machine diagrams

The state-machine diagrams of the sender and the receiver of all three protocols are drawn in #25.

## 10. Where it is implemented

| Part | Code |
|------|------|
| Packet layout, build and check | `src/common/packet.h`, `src/common/packet.c` |
| CRC-32 | `src/common/crc32.c` |
| Sender: Stop-and-Wait, Go-Back-N, Selective Repeat | `sw_run()`, `gbn_run()`, `sr_run()` in `src/sender.c` |
| Receiver: the three protocols | `sw_on_data()`, `gbn_on_data()`, `sr_on_data()` in `src/receiver.c` |
| Receive loop, linger time | `run()` in `src/receiver.c` |
| RTO estimator | `src/common/rto.h`, `src/common/rto.c` |
| Safe window limits | `max_safe_window()` in `src/common/cli.c` |
