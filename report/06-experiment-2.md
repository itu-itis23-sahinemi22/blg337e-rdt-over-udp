# 6. Experiment 2: packet loss

This experiment loses DATA packets with probability $p$ = 0, 5 and 10 % and measures how the
completion time, goodput, retransmission overhead and link utilization of the three protocols
change. Section 6.2 derives each of them from the rules of the protocols and the baseline
parameters, and section 6.5 compares the derivation with the measurements.

## 6.1 Parameters and methodology

Only the DATA loss probability changes. Every other parameter keeps its baseline value: the
256 KiB test file, a maximum payload of 1024 bytes, 1 Mbit/s and a queue of 128 packets per
direction, 20 ms propagation delay, $k = 7$, and $W = 16$ for Go-Back-N and Selective Repeat. ACKs
are not lost.

| $p$  | Scenario file                 | Seeds                                                       | Runs per protocol |
|------|-------------------------------|-------------------------------------------------------------|-------------------|
| 0    | `scenarios/baseline.conf`     | 337                                                         | 1                 |
| 5 %  | `scenarios/exp2-loss5.conf`   | 3141592653, 2718281828, 1414213562, 1732050807, 2236067977 | 5                 |
| 10 % | `scenarios/exp2-loss10.conf`  | the same five                                               | 5                 |

At $p = 0$ nothing in a run is random, so five seeds would give five identical runs; it runs once.
This gives 33 runs, which `scripts/run_all.sh exp2` writes to `runs/exp2/`.

The metrics are those of section 4, computed from the logs by the analysis scripts; for each
protocol and $p$ we give the mean and the standard deviation over the five seeds. The number of
losses in a run is itself random: the 256 packets of the file lose 13.5 transmissions on average at
$p$ = 5 %, but one seed may lose 10 and another 18. Each run is therefore compared with the
derivation twice: with the expected value for its $p$, and with the value the same formulas give
for the number of timeouts and transmissions the run actually had. The second comparison tests the
model without the sampling noise.

## 6.2 Derivation

### Quantities and assumptions

| Symbol | Meaning | Value |
|--------|---------|-------|
| $N$ | packets in the file, 262144 / 1024 | 256 |
| $T$ | link time of a DATA packet: 1036 bytes (12-byte header and 1024-byte payload) at 1 Mbit/s | 8.288 ms |
| $T_a$ | link time of an ACK, 12 bytes | 0.096 ms |
| $\tau$ | propagation delay, each way | 20 ms |
| $\mathit{RTT}_ 0 = T + 2\tau + T_a$ | RTT of a packet that finds the queue empty | 48.384 ms |
| $W$ | window of Go-Back-N and Selective Repeat | 16 |

The derivation assumes that:

1. every DATA transmission, a first one or a retransmission, is lost independently with
   probability $p$; a packet therefore needs $1/(1-p)$ transmissions on average, $p/(1-p)$ of them
   lost;
2. the emulator decides the loss when the packet arrives, before its queue (section 3), so a lost
   packet takes no link time;
3. the link time covers our header and the payload but not the UDP and IP headers, and the three
   programs take no time to process a packet.

### The RTO at the baseline

Stop-and-Wait has one packet in flight, so every RTT sample is $\mathit{RTT}_ 0$. The first sample
sets $\mathit{RTTVAR} = \mathit{RTT}_ 0 / 2$, and $4 \cdot \mathit{RTTVAR}$ = 96.8 ms is already
below $\mathit{RTO}_ {\min}$ = 200 ms; every later sample equals $\mathit{SRTT}$, so
$\mathit{RTTVAR}$ only shrinks. By section 2.5 the RTO is

$$
R_{\mathrm{SW}} = \mathit{RTT}_ 0 + \mathit{RTO}_ {\min} = 248.4\ \text{ms} .
$$

A window of 16 packets holds $W T$ = 132.6 ms of link time, more than $\mathit{RTT}_ 0$, so the
link is the bottleneck: the sender always has $W$ packets outstanding, and the link sends one every
$T$. By Little's law each packet's RTT is then $W T$; the 84 ms above $\mathit{RTT}_ 0$ are spent
in the emulator's queue. All samples are again equal, and

$$
R_W = W T + \mathit{RTO}_ {\min} = 332.6\ \text{ms} .
$$

### Backoff

Every timeout doubles the RTO, up to $\mathit{RTO}_ {\max}$ = 2 s, and only the next RTT sample
undoes it; under Karn's rule only a packet sent once gives a sample. Let $c$ be the probability
that, after a timeout, the next timeout comes before the next sample. Timeouts then come in chains,
and the $j$-th timeout of a chain waits $\min(2^{j-1} R, \mathit{RTO}_ {\max})$. A fraction
$(1-c)\,c^{j-1}$ of all timeouts is the $j$-th of its chain, so the mean RTO at a timeout is

$$
\bar R = \sum_{j \ge 1} (1-c)\, c^{j-1} \min\bigl(2^{j-1} R,\ \mathit{RTO}_ {\max}\bigr) .
$$

$c$ depends on the protocol, and it is the main reason why the three protocols pay so differently
for the same losses.

### Stop-and-Wait

Each packet takes one $\mathit{RTT}_ 0$ to be acknowledged, plus one RTO for each of its lost
transmissions. After a timeout the RTO stays doubled until a packet gets through on its first
transmission. The chain therefore goes on if the retransmission is lost, or if it arrives and the
next packet's first transmission is lost: $c_{\mathrm{SW}} = p + (1-p)\,p = 1 - (1-p)^2$, which
gives $\bar R_{\mathrm{SW}}$ = 278.2 ms at $p$ = 5 % and 320.3 ms at 10 %. The completion time is

$$
C_{\mathrm{SW}} = N \Bigl( \mathit{RTT}_ 0 + \frac{p}{1-p}\, \bar R_{\mathrm{SW}} \Bigr) .
$$

Every lost transmission is retransmitted once: per packet, $p/(1-p)$ retransmissions out of
$1/(1-p)$ transmissions, so the retransmission overhead is exactly $p$.

### Selective Repeat

Without loss the link never idles. The last packet leaves the link after $N T$ and its ACK arrives
$2\tau + T_a$ later, so $C_0 = N T + 2\tau + T_a$ = 2161.8 ms.

A lost packet $i$, sent at time $s$, holds the window. The ACKs of the packets before it keep
arriving, so the sender still sends $i+1, \ldots, i+W-1$, the last one at $s + (W-1)T$, and then
waits. Each of these packets is acknowledged $W T$ after it was sent, and the last $2\tau + T_a$ of
that is propagation and the ACK, so the link finishes them at $s + (2W-1)T - 2\tau - T_a$. It is
then idle until packet $i$'s timer expires at $s + R_W$. The retransmission takes $T$, and the
window moves only when its ACK arrives, $\mathit{RTT}_ 0$ after it was sent, so the link is idle for
another $2\tau + T_a$. Each loss therefore adds

$$
\Delta_{\mathrm{SR}} = R_W - (2W-1)\,T + 2\,(2\tau + T_a) = 155.9\ \text{ms}
$$

to the completion time, while the link time stays $N T$: the lost transmission took none, and the
retransmission takes its place. The backoff hardly matters here, because the packets after $i$ were
sent once and their individual ACKs keep giving samples; we take $c_{\mathrm{SR}} = 0$. Hence

$$
C_{\mathrm{SR}} = N T + 2\tau + T_a + N \frac{p}{1-p}\, \Delta_{\mathrm{SR}} ,
$$

and since only the lost packets are resent, the retransmission overhead is $p$, as in
Stop-and-Wait.

This treats every loss on its own. With $W p$ = 0.8 and 1.6 losses per window at 5 and 10 %, two
losses often fall into the same window. They then share one idle period, which makes the transfer
faster than the model; but the first timeout also doubles the RTO that the second packet's timer
uses (section 2.5), which makes it slower.

### Go-Back-N

A lost packet makes the receiver discard the $W - 1$ packets sent after it, and the timeout resends
all $W$. Let $n$ be the expected number of transmissions spent on the oldest unacknowledged packet,
counting the packets resent with it. Its transmission either arrives, with probability $1 - p$, or
is lost and wastes $W$ transmissions before the same situation repeats, so $n = (1-p) + p\,(W + n)$:

$$
n_{\mathrm{GBN}} = \frac{1 - p + W p}{1 - p} ,
\qquad
\text{overhead} = \frac{n_{\mathrm{GBN}} - 1}{n_{\mathrm{GBN}}} = \frac{W p}{1 - p + W p} ,
$$

45.7 % at $p$ = 5 % and 64.0 % at 10 %. Each lost transmission of the oldest packet causes one
timeout, $N p / (1-p)$ in all, as in the other two protocols.

The timer of packet $i$ only starts when $i$ becomes the oldest unacknowledged packet, that is when
the ACK of packet $i - 1$ arrives at $s + (W-1)T$, so it expires $(W-1)T$ later than in Selective
Repeat. The link finishes the packets after $i$ at the same moment as above, and is idle for
$\bar R_{\mathrm{GBN}} - W T + 2\tau + T_a$ per timeout. The $W$ resent packets then keep the link
busy for $W T > \mathit{RTT}_ 0$, long enough for their ACKs to bring new packets, so there is no
second idle period. The link carries every transmission that is not lost, $N(1 - p + W p)$ of them,
so

$$
C_{\mathrm{GBN}} = N (1 - p + W p)\, T + N \frac{p}{1-p} \bigl( \bar R_{\mathrm{GBN}} - W T + 2\tau + T_a \bigr) + 2\tau + T_a .
$$

The backoff decides the result. After a timeout every outstanding packet has been resent, so the
next sample can only come from a packet sent for the first time after it, and that packet is
acknowledged only if all $W$ resent packets arrive. If any of them is lost, the next timeout waits
the doubled RTO:

$$
c_{\mathrm{GBN}} = 1 - (1-p)^W ,
$$

56.0 % at $p$ = 5 % and 81.5 % at 10 %. Long chains are then common: $\bar R_{\mathrm{GBN}}$ is
844.9 ms at 5 % and 1407.2 ms at 10 %, against an undoubled $R_W$ of 332.6 ms.

### Goodput and link utilization

The goodput is $G = 8 S / C$ with $S$ = 262144 bytes. The link utilization counts only new data:
the first transmissions that are not lost, $N (1-p)$ of them, each on the link for $T$, so
$U = N (1-p)\, T / C$. In Go-Back-N this includes the new packets that the receiver discards after a
loss, since they cross the link for the first time.

## 6.3 Expected outcome

| Protocol | $p$  | Completion time | Goodput      | Retransmission overhead | Link utilization | Timeouts |
|----------|------|-----------------|--------------|-------------------------|------------------|----------|
| SW       | 0    | 12.39 s         | 169.3 kbit/s | 0                       | 17.1 %           | 0        |
| SW       | 5 %  | 16.14 s         | 130.0 kbit/s | 5.0 %                   | 12.5 %           | 13.5     |
| SW       | 10 % | 21.50 s         | 97.5 kbit/s  | 10.0 %                  | 8.9 %            | 28.4     |
| GBN      | 0    | 2.16 s          | 970.1 kbit/s | 0                       | 98.1 %           | 0        |
| GBN      | 5 %  | 13.89 s         | 151.0 kbit/s | 45.7 %                  | 14.5 %           | 13.5     |
| GBN      | 10 % | 42.74 s         | 49.1 kbit/s  | 64.0 %                  | 4.5 %            | 28.4     |
| SR       | 0    | 2.16 s          | 970.1 kbit/s | 0                       | 98.1 %           | 0        |
| SR       | 5 %  | 4.26 s          | 492.1 kbit/s | 5.0 %                   | 47.3 %           | 13.5     |
| SR       | 10 % | 6.60 s          | 318.0 kbit/s | 10.0 %                  | 29.0 %           | 28.4     |

All three protocols see the same number of timeouts on average; what differs is what each timeout
costs.

- **Selective Repeat** stays the fastest. A loss costs one stall of about 156 ms, and only the lost
  packet is resent, so its overhead equals $p$.
- **Stop-and-Wait** pays one RTO of about 250 to 320 ms per loss on top of its 256 RTTs, which makes
  it 30 % slower at 5 % and 74 % slower at 10 %. Its overhead also equals $p$.
- **Go-Back-N** pays the most. It resends about $W$ packets per loss, so its overhead is 9 times $p$
  at 5 % and 6 times $p$ at 10 %, and every resent window that loses a packet doubles the RTO again.
  At 10 % it is expected to be slower than Stop-and-Wait: pipelining no longer helps once most of the
  time is spent waiting for timeouts of up to 2 s. For the same reason its completion time should
  vary the most between seeds, since a few long chains of timeouts dominate it.

## 6.4 Results

*To be filled in from `runs/exp2/` and its `metrics.csv`.*

Raw data, one row per run:

| Protocol | $p$ | Seed | Completion time | Goodput | Retransmission overhead | Link utilization | Timeouts | Hashes match |
|----------|-----|------|-----------------|---------|-------------------------|------------------|----------|--------------|

Mean and standard deviation over the seeds, next to the expected values of section 6.3 and the
values the model gives for each run's own number of timeouts:

| Protocol | $p$ | Completion time | Goodput | Retransmission overhead | Link utilization |
|----------|-----|-----------------|---------|-------------------------|------------------|

Plots: goodput against $p$ and retransmission overhead against $p$ for the three protocols, measured
(mean and standard deviation) and expected.

## 6.5 Interpretation

*To be written after the runs: compare 6.4 with 6.3, protocol by protocol.*
