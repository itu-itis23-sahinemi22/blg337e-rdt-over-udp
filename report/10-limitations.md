# 10. Failure conditions and limitations

This section lists the conditions under which the design fails, and the limitations it accepts in
exchange for simplicity. Each item names the cause, what happens, and where it is shown or what
would remove it. Section 2 describes the design itself.

## 10.1 When a transfer fails or delivers a wrong file

**Windows above the sequence-number limit.** With $W > 2^k - 1$ (Go-Back-N) or $W > 2^{k-1}$
(Selective Repeat), the receiver can take a retransmitted old packet for a new one (section 2.3).
Such windows are accepted with a warning, because Experiment 5 needs them. The dangerous part is
that the failure is silent: both programs can still exit with status 0, and only the comparison of
the SHA-256 hashes shows that the file is wrong. Experiment 5 (section 9) derives the limits and
reproduces the failures.

**Packets that outlive the sequence space.** The window limits assume that every packet arrives
before its sequence number comes round again. The emulator never violates this on its own: jitter
does not change the delivery order, reordering moves a packet by one position, and a duplicate
follows its original immediately. A per-packet delay action, however, could hold one packet back
while later packets overtake it. If such a packet arrives once the receiver has moved on by about
$2^k$ sequence numbers ($2^k - W + 1$ suffice for Selective Repeat), its number matches one the
receiver expects, and old data is written as new, even with a safe window. With a full pipeline at
the baseline link rate this takes a delay of about one second at $k = 7$, and only a few tens of
milliseconds at $k = 3$. TCP prevents this with a maximum segment lifetime and a 32-bit sequence
space; with $k \le 8$ our sequence space is too small for such a guarantee. Experiment 5 therefore
has to produce its failures with an impairment that keeps packets within the sequence space, such
as lost ACKs: long delays would break the safe windows too.

**No connection identifier.** There is no connection set-up, and packets carry no identifier of
the transfer they belong to. The receiver accepts every valid DATA packet that reaches its port, so
a sender or an emulator left over from an earlier run on the same ports would mix its packets into
the transfer. The protocol also does not check that both ends were started with the same `-p`, `-w`
and `-k`; a mismatch makes the transfer fail or deliver a wrong file. The run scripts are
responsible for both: fresh processes for every run and the same parameters on both ends.

**Persistent loss.** The sender gives up when a packet is still not acknowledged after $x$
retransmissions (`-x`, 10 by default), so that it does not retry forever once the receiver is gone.
The receiver then exits with status 1 after its linger time and leaves the incomplete file on
disk. With Stop-and-Wait and Selective Repeat, a packet gives up only if its first transmission and
all $x$ retransmissions fail. If each attempt fails independently with probability $q$ (the DATA
packet or its ACK is lost or corrupted), a transfer of $N$ packets gives up with probability

$$
P_ {\text{give up}} = 1 - \left(1 - q^{x+1}\right)^N .
$$

For the 256-packet test file and $x = 10$ this is about $3 \times 10^{-9}$ at $q = 0.1$,
$5 \times 10^{-4}$ at $q = 0.3$ and $0.12$ at $q = 0.5$.

Go-Back-N reaches the limit much earlier. A timeout resends every outstanding packet and counts a
retransmission for each of them, and the sender gives up when the count of the oldest packet
reaches $x$. A packet therefore also collects the timeouts caused by the losses of the packets
before it in the window, not only its own. With $W = 16$ this is rare at the 10 % loss of
Experiment 2 but becomes frequent at higher loss rates. Counting timeouts in a row without progress,
reset whenever an ACK moves the window, would still end a transfer whose receiver is gone without
this effect.

**Lost final ACKs.** After the last packet, the receiver keeps acknowledging retransmissions for
its linger time `-q`. If the ACKs of all $x + 1$ transmissions of the last packet are lost, the
sender gives up and exits with status 1 although the receiver has the complete file and exits with
0. At 5 % ACK loss this has a probability of about $0.05^{11} \approx 5 \times 10^{-15}$. The same
happens if `-q` is shorter than the sender's longest silence (section 2.6), for example when `-x`
is raised without raising `-q`. The exit status of the sender therefore does not decide whether a
transfer succeeded; the SHA-256 comparison does.

**RTTs above the maximum RTO.** $\mathit{RTO}_ {\max}$ is 2 s. Above an RTT of about 1.8 s the RTO
keeps less than $\mathit{RTO}_ {\min}$ of margin, so jitter starts to cause needless
retransmissions. Above 2 s every packet times out before its ACK arrives and is sent at least
twice, and Karn's rule leaves no RTT samples, so the RTO stays at its maximum. The transfer still
completes, at a much lower goodput, as long as the RTT stays below about $(x + 1) \times 2$ s
= 22 s; beyond that the sender gives up. The largest RTT in our experiments is about 1 s.

**Start order.** The receiver waits without a time limit for the first datagram, so a receiver
whose sender never starts runs until the run script stops it. The other way round, a sender with no
receiver behind the emulator gives up after about 21 s with the defaults: the initial RTO of 1 s,
then 10 retransmissions 2 s apart. The run scripts therefore start the receiver first.

## 10.2 Performance limitations

**Every loss costs a timeout.** The sender learns about a loss only when a timer expires. Go-Back-N
ignores repeated ACKs instead of using them for a fast retransmission, there are no negative
acknowledgments, and a corrupted packet is dropped exactly like a lost one. Each loss therefore
costs at least $\mathit{SRTT} + \mathit{RTO}_ {\min}$ of waiting, which the derivations of
Experiment 2 (section 6) have to include.

**The minimum RTO on short paths.** $\mathit{RTO}_ {\min} = 200$ ms keeps needless retransmissions
away (section 2.5), but at the baseline, where the RTT of Stop-and-Wait is about 48 ms, every loss
costs about 5 RTTs. A smaller `-r` lowers this cost at the risk of retransmitting packets whose ACK
is only late.

**One RTO for all Selective Repeat timers.** A timeout doubles the RTO for every running timer, not
only for the packet that timed out. If two packets of one window are lost, the second one is resent
later than its own RTO would require, until the next RTT sample undoes the backoff. Go-Back-N has a
similar delay for a different reason: after a timeout every outstanding packet has been
retransmitted, so the backed-off RTO stays until a packet sent for the first time after the timeout
is acknowledged.

**A fixed window and no congestion control.** The sender always keeps up to $W$ packets
outstanding, whatever the state of the link, and sends the first window in one burst; Go-Back-N
also resends a whole window in one burst after every timeout. The receiver can store a whole
window, so no flow control is needed, but nothing reduces the window when the emulator's queue
overflows. With $k = 8$ the window can be larger than the queue's 128 packets: up to 255 with
Go-Back-N within the safe limit.

**A small sequence space.** With $k \le 8$ the window is at most 255 packets (Go-Back-N) or 128
(Selective Repeat), so a single transfer can never exceed $W m / \mathit{RTT}$ bytes per second,
where $m$ is the payload size. This is enough for every link in our experiments, but not for a path
whose bandwidth-delay product exceeds the window.

**The linger time.** The receiver exits only after `-q` (25 s) of silence. This does not affect the
completion time, which ends at the sender, but adds 25 s to the duration of every run. A smaller
`-q` is safe only together with a smaller `-x` (section 2.6).

## 10.3 Limits of the measurements

**RTT samples of Go-Back-N can be too long.** Go-Back-N samples the newest packet that a cumulative
ACK acknowledges. If the first copy of that ACK was lost, and the next packet was lost or delayed
too, the ACK the sender gets is a repeat sent when a later packet arrived, and the sample includes
the time between the two arrivals. This needs two impairments at once and only makes the RTO
larger.

**Timer resolution and scheduling.** Timers wait in `poll()`, which takes milliseconds; the time
left is rounded up, so a timer fires up to 1 ms late. All three programs run on one machine and
share its CPU, so every measured RTT also contains a little scheduling delay.

**Bursts and socket buffers.** A sender sends its whole window at once. A packet dropped because
a socket receive buffer is full is lost outside the emulator's model and does not appear in its
log. The emulator must therefore have a receive buffer that holds a whole window (section 3).

## 10.4 Outside the scope of the design

- The sender reads the whole file into memory, so the file size is limited by the memory available.
- CRC-32 detects transmission errors, not deliberate changes: it detects every single-bit error and
  every burst of up to 32 bits, but anyone who can send to the receiver's port can build a packet
  with a valid CRC. There is no authentication.
- The programs use IPv4 on the loopback interface, and each process carries out one transfer.
