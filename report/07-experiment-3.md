# 7. Experiment 3: propagation delay

This experiment raises the propagation delay from 20 to 200 ms and measures two things: how large a
window must be to keep the link busy, and how the RTO estimator follows long and varying RTTs.
Section 7.2 derives the completion time, the link utilization and the RTO from the protocol and the
baseline parameters, and section 7.6 compares the derivation with the measurements.

## 7.1 Parameters and methodology

The propagation delay $\tau$ takes the values 20, 50, 100 and 200 ms, in both directions, and the
jitter $j$ is 10 % of it in both directions: 2, 5, 10 and 20 ms. Stop-and-Wait runs at every delay,
and so does Selective Repeat with $W$ = 16 and $W$ = 64. Every other parameter keeps its baseline
value: the 256 KiB test file, a maximum payload of 1024 bytes, 1 Mbit/s and a queue of 128 packets
per direction, no loss or other impairment, and $k = 7$. $W = 64$ is the largest safe window of
Selective Repeat for $k = 7$, $2^{k-1}$, and the queue of 128 packets holds a whole window of it.

| $\tau$ | $j$   | Scenario file                  |
|--------|-------|--------------------------------|
| 20 ms  | 2 ms  | `scenarios/exp3-delay20.conf`  |
| 50 ms  | 5 ms  | `scenarios/exp3-delay50.conf`  |
| 100 ms | 10 ms | `scenarios/exp3-delay100.conf` |
| 200 ms | 20 ms | `scenarios/exp3-delay200.conf` |

The jitter is the only random part of a run. Each of the 12 configurations runs with the seeds
3141592653, 2718281828 and 1414213562, which gives 36 runs; `scripts/run_all.sh exp3` writes them
to `runs/exp3/`. We give the mean and the standard deviation of each metric of section 4 over the
three seeds. The RTT and RTO plots use the `rtt_sample` and `rto_update` events of the sender's log
(section 2.5) of the first seed.

## 7.2 Derivation

The symbols are those of section 6.2: $T$ = 8.288 ms and $T_a$ = 0.096 ms are the link times of a
DATA packet and an ACK, and $N$ = 256 is the number of packets. The RTT of a packet that finds the
queue empty grows with the delay:

$$
\mathit{RTT}_ 0(\tau) = T + 2\tau + T_a ,
$$

48.4, 108.4, 208.4 and 408.4 ms for the four delays. The derivation assumes, as in section 6.2,
that the programs take no time to process a packet, and it first ignores the jitter, whose mean is
zero; its effects are discussed at the end of this section.

### The window that keeps the link busy

The first packet of a window is acknowledged $\mathit{RTT}_ 0$ after it starts on the link. If the
link has had something to send all that time, it never idles; so the link stays busy exactly when
the window holds at least one RTT of link time, $W T \ge \mathit{RTT}_ 0$, and the smallest such
window is

$$
W_{\min} = \left\lceil \frac{\mathit{RTT}_ 0}{T} \right\rceil = 6,\ 14,\ 26,\ 50
$$

for the four delays. A smaller window leaves the link idle for $\mathit{RTT}_ 0 - W T$ in every RTT,
and in the steady state the link is busy for the fraction

$$
U_{\infty} = \min\Bigl(1,\ \frac{W T}{\mathit{RTT}_ 0}\Bigr)
$$

of the time. $W = 16$ therefore fills the link at 20 and 50 ms but only 63.6 % of it at 100 ms and
32.5 % at 200 ms, while $W = 64$ fills it at every delay. With $k = 7$, the safe limit of Selective
Repeat, $W \le 64$, keeps the link full up to $\mathit{RTT}_ 0 = 64\,T$ = 530.4 ms, that is
$\tau \le$ 261 ms.

### Completion time and link utilization

At the start the sender sends $W$ packets at once, and the link sends them one after the other.
The ACK of packet $m$ arrives $\mathit{RTT}_ 0$ after it started on the link and releases packet
$m + W$. If $W T < \mathit{RTT}_ 0$, the link has finished the whole window by then, so packet
$m + W$ starts exactly $\mathit{RTT}_ 0$ after packet $m$: the packets go out in groups of $W$, one
group per $\mathit{RTT}_ 0$. If $W T \ge \mathit{RTT}_ 0$, packet $m + W$ waits in the queue until
the link has sent the $W - 1$ packets before it, and the groups follow each other after $W T$. In
both cases the $N / W$ groups start $\max(W T, \mathit{RTT}_ 0)$ apart, the last packet starts
$(W-1)\,T$ after its group, and its ACK arrives $\mathit{RTT}_ 0$ after that:

$$
C = \Bigl(\frac{N}{W} - 1\Bigr) \max(W T,\ \mathit{RTT}_ 0) + (W - 1)\,T + \mathit{RTT}_ 0 .
$$

For Stop-and-Wait, $W = 1$, this is $C = N \cdot \mathit{RTT}_ 0$; for a window that fills the
link it is $C = N T + 2\tau + T_a$, the link time of the file plus the propagation of the last
packet and its ACK. The goodput is $8 S / C$, and since nothing is lost or resent, the link
utilization is $U = N T / C$. $U$ stays below $U_{\infty}$ because the link is still idle while the
last ACK comes back: with $W = 64$ at 200 ms the link is full until the last packet, but $U$ is
84.1 %, since the transfer lasts only 2.5 s and the last 0.4 s of it is the final RTT.

### RTT and RTO

In the steady state each RTT sample is

- $\mathit{RTT}_ 0$ when the queue is empty: Stop-and-Wait, and Selective Repeat with a window that
  does not fill the link;
- $W T$ when the window fills the link: by Little's law, as in section 6.2, $W$ packets are
  outstanding and the link sends one every $T$. The packets beyond $W_{\min}$ only wait in the
  emulator's queue. With $W = 64$ the RTT is therefore 530.4 ms at every delay of this experiment.

The jitter adds $J_1 + J_2$, the offsets of the two directions, each uniform in $[-j, j]$. Their sum
has a triangular distribution on $[-2j, 2j]$, with standard deviation $j \sqrt{2/3}$ and mean
absolute value $2j/3$. $\mathit{RTTVAR}$ estimates this mean absolute deviation, so
$4 \cdot \mathit{RTTVAR} \approx 8j/3$: 5.3, 13.3, 26.7 and 53.3 ms. Each of these is below
$\mathit{RTO}_ {\min}$ = 200 ms, so after the first few samples the margin above $\mathit{SRTT}$ is
$\mathit{RTO}_ {\min}$ (section 2.5), and

$$
\mathit{RTO} = \mathit{SRTT} + \mathit{RTO}_ {\min} .
$$

The RTO follows the RTT at a constant distance of 200 ms. An RTT exceeds its mean by at most
$2j$ = 40 ms, far less than this margin, so no timer should expire: every run is expected to finish
without a retransmission.

At the start the estimator knows nothing. The first sample $R$, which always finds the queue empty,
sets $\mathit{RTTVAR} = R/2$, so the RTO jumps to $R + \max(\mathit{RTO}_ {\min}, 2R)$: 248.4, 325.2,
625.2 and 1225.2 ms. Each later sample multiplies $\mathit{RTTVAR}$ by about 3/4, so the $2R$ term
falls below $\mathit{RTO}_ {\min}$ after the smallest $n$ with $2R\,(3/4)^n < \mathit{RTO}_ {\min}$:
1, 3 and 5 samples at 50, 100 and 200 ms. With $W = 64$ the RTTs of the first window grow from
$\mathit{RTT}_ 0$ to $\mathit{RTT}_ 0 + 63\,T$ while the queue fills (section 2.5); $\mathit{RTTVAR}$
grows with them, and the RTO then settles at $W T + \mathit{RTO}_ {\min}$ = 730.4 ms.

The standard estimator of RFC 6298 would keep the RTO only $8j/3$ above the RTT, 5 to 53 ms; ours
stays 200 ms above it. This is the price of the margin of section 2.5: a lost packet waits up to
200 ms longer for its timeout, but a sudden rise of the RTT by less than 200 ms causes no needless
retransmission.

### The effect of jitter on the order of packets

The emulator never lets a packet overtake the one before it (section 3): a packet whose offset would
make it arrive first is held until its predecessor has arrived. This happens only when packets
follow each other more closely than $2j$. With Stop-and-Wait they are an RTT apart, so it never
happens. With Selective Repeat they follow each other $T$ = 8.3 ms apart, closer than $2j$ from
50 ms on; at 100 and 200 ms, where $2j$ is 20 and 40 ms, the holding makes the mean RTT a few
milliseconds longer. The formula for $C$
ignores this, so we expect Selective Repeat with $W = 16$ at 100 and 200 ms, whose completion time
is 16 RTTs, to take a few percent longer than the formula.

## 7.3 Go-Back-N

Go-Back-N is not run in this experiment. Without loss, and with a jitter that keeps the order of
the packets, its receiver gets every packet in order, so it never discards one and the sender never
times out. Each ACK then acknowledges exactly one more packet, as in Selective Repeat, and releases
one new packet, so Go-Back-N with the same $W$ sends the same packets at the same times and takes
the same RTT samples. We therefore expect exactly the completion times, utilizations and RTOs of
Selective Repeat in section 7.4. $W = 64$ is safe for Go-Back-N as well; its limit for $k = 7$ is
127.

The difference appears as soon as a packet is lost. Go-Back-N then resends the whole window
(section 6.2), and the window it needs grows with the delay: at 200 ms it must hold at least 50
packets to keep the link busy, so every loss would cost about 50 transmissions instead of one, and
a timeout chain of section 6.2 becomes more likely as well, since any of these 50 resent packets
can be lost. The longer the delay, the more a loss costs Go-Back-N compared with Selective Repeat.

## 7.4 Expected outcome

Completion time, goodput and link utilization:

| $\tau$ | Protocol | $W$ | Completion time | Goodput      | Link utilization $U$ | $U_{\infty}$ |
|--------|----------|-----|-----------------|--------------|----------------------|--------------|
| 20 ms  | SW       | 1   | 12.39 s         | 169.3 kbit/s | 17.1 %               | 17.1 %       |
| 20 ms  | SR       | 16  | 2.16 s          | 970.1 kbit/s | 98.1 %               | 100 %        |
| 20 ms  | SR       | 64  | 2.16 s          | 970.1 kbit/s | 98.1 %               | 100 %        |
| 50 ms  | SW       | 1   | 27.75 s         | 75.6 kbit/s  | 7.6 %                | 7.6 %        |
| 50 ms  | SR       | 16  | 2.22 s          | 943.9 kbit/s | 95.5 %               | 100 %        |
| 50 ms  | SR       | 64  | 2.22 s          | 943.9 kbit/s | 95.5 %               | 100 %        |
| 100 ms | SW       | 1   | 53.35 s         | 39.3 kbit/s  | 4.0 %                | 4.0 %        |
| 100 ms | SR       | 16  | 3.46 s          | 606.4 kbit/s | 61.3 %               | 63.6 %       |
| 100 ms | SR       | 64  | 2.32 s          | 903.2 kbit/s | 91.4 %               | 100 %        |
| 200 ms | SW       | 1   | 104.55 s        | 20.1 kbit/s  | 2.0 %                | 2.0 %        |
| 200 ms | SR       | 16  | 6.66 s          | 315.0 kbit/s | 31.9 %               | 32.5 %       |
| 200 ms | SR       | 64  | 2.52 s          | 831.6 kbit/s | 84.1 %               | 100 %        |

What depends only on the delay and the jitter:

| $\tau$ | $\mathit{RTT}_ 0$ | $W_{\min}$ | Standard deviation of the RTT | $4 \cdot \mathit{RTTVAR}$ | RTO after the first sample | Samples until the margin is $\mathit{RTO}_ {\min}$ |
|--------|-------------------|------------|-------------------------------|---------------------------|----------------------------|----------------------------------------------------|
| 20 ms  | 48.4 ms           | 6          | 1.6 ms                        | 5.3 ms                    | 248.4 ms                   | 0                                                  |
| 50 ms  | 108.4 ms          | 14         | 4.1 ms                        | 13.3 ms                   | 325.2 ms                   | 1                                                  |
| 100 ms | 208.4 ms          | 26         | 8.2 ms                        | 26.7 ms                   | 625.2 ms                   | 3                                                  |
| 200 ms | 408.4 ms          | 50         | 16.3 ms                       | 53.3 ms                   | 1225.2 ms                  | 5                                                  |

RTT and RTO in the steady state, RTT / RTO in ms:

| $\tau$ | SW            | SR, $W$ = 16  | SR, $W$ = 64  |
|--------|---------------|---------------|---------------|
| 20 ms  | 48.4 / 248.4  | 132.6 / 332.6 | 530.4 / 730.4 |
| 50 ms  | 108.4 / 308.4 | 132.6 / 332.6 | 530.4 / 730.4 |
| 100 ms | 208.4 / 408.4 | 208.4 / 408.4 | 530.4 / 730.4 |
| 200 ms | 408.4 / 608.4 | 408.4 / 608.4 | 530.4 / 730.4 |

- **Stop-and-Wait** spends one RTT per packet, so its completion time grows linearly with the delay,
  from 12.4 s to 104.5 s, and its utilization falls from 17.1 % to 2.0 %.
- **Selective Repeat with $W$ = 16** keeps the link full up to 50 ms. Beyond that the window is
  smaller than $W_{\min}$, the transfer becomes 16 rounds of one RTT, and the utilization falls with
  the delay.
- **Selective Repeat with $W$ = 64** keeps the link full at every delay; its completion time only
  grows by the final RTT. Its RTT is 530 ms at every delay, because the packets that the path does
  not need wait in the queue.
- **The RTO** starts high after the first sample, settles within a few samples, and then stays
  200 ms above the smoothed RTT, wherever the delay puts it. No run should retransmit.

## 7.5 Results

*To be filled in from `runs/exp3/` and its `metrics.csv`.*

Raw data, one row per run:

| $\tau$ | Protocol | $W$ | Seed | Completion time | Goodput | Link utilization | Retransmissions | Mean RTT | Hashes match |
|--------|----------|-----|------|-----------------|---------|------------------|-----------------|----------|--------------|

Mean and standard deviation over the seeds, next to the expected values of section 7.4:

| $\tau$ | Protocol | $W$ | Completion time | Goodput | Link utilization |
|--------|----------|-----|-----------------|---------|------------------|

Plots: link utilization against the delay for the three configurations, measured and expected; and
for each delay, the RTT samples, $\mathit{SRTT}$ and the RTO over the course of the transfer, for
Stop-and-Wait and for Selective Repeat with $W$ = 64.

## 7.6 Interpretation

*To be written after the runs: compare 7.5 with 7.4, and the RTO plots with the RTT and RTO
behaviour derived in section 7.2.*
