# 9. Experiment 5: sequence-number limits

This experiment derives the largest window that Go-Back-N and Selective Repeat can use with a
$k$-bit sequence number, confirms the limit for $k = 3$ and $k = 4$, and shows a failure one window
above it. A failure means that the received file differs from the original, shown by their SHA-256
hashes. This is the only experiment in which failures are expected.

## 9.1 Derivation

As in section 2.3, packet $i$ carries the number $i \bmod 2^k$, and the receiver sees only that
number. A window size is safe if, at every moment, every number that can arrive is read as the
packet it really is. The derivation assumes that packets arrive in the order they were sent. A
packet held back for long breaks even the safe windows (section 10.1); the scenarios below only
drop packets, so they keep to the assumption.

**Go-Back-N.** Let $e$ be the index of the packet the receiver expects next. Take any packet that
arrives, and let $b$ be the sender's base when it sent that packet, the oldest packet it had not
seen acknowledged; the packet's index lies in $[b, b + W - 1]$. An ACK can only report what the
receiver has, so $b \le e$. Every packet the receiver has was sent before this one, when the base
was no larger than $b$, so it is at most packet $b + W - 1$, and $e \le b + W$. Together,

$$
e - W \le b \le e ,
$$

so the arriving packet has an index in $[e - W, e + W - 1]$. The receiver accepts a packet if and only if its number equals $e \bmod 2^k$, so it fails if any other
index in this range has the same number, that is, if $e - 2^k$ or $e + 2^k$ lies in it. Since
$W \le 2^k$ (section 5.2 of `docs/interfaces.md`), $e + 2^k > e + W - 1$, and only the old packet
$e - 2^k$ can do it, which requires $e - 2^k \ge e - W$. Go-Back-N is therefore safe if and only if

$$
W \le 2^k - 1 .
$$

The sender gives the same bound. The ACK number is the receiver's $e$, which can take the $W + 1$
values $b, \ldots, b + W$; they are distinct only if $W + 1 \le 2^k$. At $W = 2^k$ the ACK
$b \bmod 2^k$ can mean either "nothing new" or "the whole window". Our sender always reads it as
"nothing new" (section 2.4), so in the runs below the failure comes from the receiver.

**Selective Repeat.** Let the receive window start at $r$, the oldest packet not yet received, so
that it holds $r, \ldots, r + W - 1$. For an arriving packet, let $b$ again be the sender's base
when it sent the packet. The sender cannot have had an ACK for packet $r$, so $b \le r$, and every
packet the receiver has was sent before, at most packet $b + W - 1$, so $r \le b + W$. Hence
$r - W \le b \le r$, and the arriving packet has an index in $[r - W, r + W - 1]$. The receiver must store the new packets
$r, \ldots, r + W - 1$ and acknowledge the old ones $r - W, \ldots, r - 1$ again (section 2.4).
These are $2W$ consecutive indices, and they have distinct numbers only if $2W \le 2^k$, so Selective
Repeat is safe if and only if

$$
W \le 2^{k-1} .
$$

Above the limit, an old packet $i$ has the same number as the packet $i + 2^k$ inside the window.
The receiver checks the window first, so it stores the old packet as a new one.

In both protocols the worst case is $b = e - W$ (or $b = r - W$): the receiver has the whole window,
and the sender has none of its ACKs. The scenarios produce exactly this case.

## 9.2 Scenarios

The assignment asks for every window up to the limit to succeed under the same impairments, so each
protocol and $k$ has one scenario, used for every $W$. All of them only drop ACKs, chosen by index:
"ACK $n$" is the $n$-th ACK datagram the emulator sees, retransmissions included (section 7.5 of
`docs/interfaces.md`).

**Go-Back-N: drop ACKs $1, \ldots, 2^k$.** At $W = 2^k$ these are the ACKs of the whole first
window. The receiver then expects packet $2^k$, whose number is 0. The sender sees no ACK, times out
and sends packets $0, \ldots, 2^k - 1$ again, and the receiver accepts them as packets
$2^k, \ldots, 2^{k+1} - 1$. With $W < 2^k$ the same drops only cost timeouts: the resent packets
are below the expected one, so they are discarded and acknowledged, and an ACK gets through once the
dropped ones are used up.

**Selective Repeat: drop ACK 1 only**, the ACK of packet 0. Packets $1, \ldots, W - 1$ are
acknowledged, so on its timeout the sender resends packet 0 alone. By then the receiver has packets
$0, \ldots, W - 1$, and its window is $W, \ldots, 2W - 1$. Packet 0 has the same number as packet
$2^k$, which lies in this window if and only if $2W - 1 \ge 2^k$, that is, $W \ge 2^{k-1} + 1$.
Otherwise it lies less than $W$ below the window and is acknowledged again.

| Scenario file                 | Protocol         | $k$ | Dropped ACKs        | Safe limit | Windows run |
|-------------------------------|------------------|-----|---------------------|------------|-------------|
| `scenarios/exp5-gbn-k3.conf`  | Go-Back-N        | 3   | $1, \ldots, 8$      | 7          | 1 to 8      |
| `scenarios/exp5-gbn-k4.conf`  | Go-Back-N        | 4   | $1, \ldots, 16$     | 15         | 1 to 16     |
| `scenarios/exp5-sr.conf`      | Selective Repeat | 3   | 1                   | 4          | 1 to 5      |
| `scenarios/exp5-sr.conf`      | Selective Repeat | 4   | 1                   | 8          | 1 to 9      |

## 9.3 Parameters and methodology

All other parameters keep their baseline values: the 256 KiB test file, a maximum payload of 1024
bytes, 1 Mbit/s and a queue of 128 packets per direction, 20 ms propagation delay, and no random
impairments. The scenarios use no random impairments, so the seed has no effect; it is recorded
anyway.

Every run uses `-x 20 -q 45000` instead of the defaults `-x 10 -q 25000`. The Go-Back-N scenario
for $k = 4$ needs this at $W = 1$: every timeout resends packet 0 and produces one ACK, so the 16
dropped ACKs answer the first transmission and 15 retransmissions, and only the 16th retransmission
is acknowledged. With `-x 10` the sender would give up first. A limit of 20 leaves a margin, and the
linger time follows the rule of section 4.2 of `docs/interfaces.md`:
$20 \times 2\,\text{s} + 2\,\text{s} = 42\,\text{s} < 45\,\text{s}$. Neither option is a baseline
parameter of the assignment, and both are the same for every run of this experiment.

This gives 38 runs: 8 and 16 for Go-Back-N, 5 and 9 for Selective Repeat. The windows above the
safe limit print a warning and run anyway (section 5.2 of `docs/interfaces.md`). Each failing run
is repeated three times to show that it fails in the same way every time. For each run we record
whether the SHA-256 hashes match, the size of the received file and the exit status of both
programs.

## 9.4 Expected outcome

Below and at the safe limit, every run delivers the file intact. One window above it:

- **Go-Back-N, $W = 2^k$:** the receiver writes packets $0, \ldots, 2^k - 1$ twice and then the
  rest of the file. From then on its numbering is consistent again, only shifted by $2^k$, so it
  accepts every later packet and completes on the `LAST` packet. The received file is longer by
  $2^k \times 1024$ bytes (8 KiB for $k = 3$, 16 KiB for $k = 4$), and block $2^k$ is the first that
  differs.
- **Selective Repeat, $W = 2^{k-1} + 1$:** the old packet 0 is stored as packet $2^k$ and delivered
  in its place. The real packet $2^k$ finds its slot taken or arrives below the window, and is
  discarded but acknowledged. The received file has the right size, but block $2^k$ holds the data
  of block 0.

In both cases the sender gets an ACK for every packet and the receiver completes the file, so both
programs exit with status 0. The failure is silent, as section 10.1 says: only the hashes show it.

## 9.5 Results

*To be filled in from the runs in `runs/exp5/`.*

| Protocol | $k$ | $W$ | Hashes match | Received size (bytes) | Sender / receiver status |
|----------|-----|-----|--------------|-----------------------|--------------------------|

## 9.6 Interpretation

*To be written after the runs: compare 9.5 with 9.4.*
