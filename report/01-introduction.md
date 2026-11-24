# 1. Introduction

UDP delivers datagrams without any guarantee. A datagram may be lost, delivered twice, delivered out
of order or corrupted, and the sender is never told. This project builds reliable file transfer on
top of UDP: a sender splits a file into packets and sends them to a receiver, which must write an
exact copy of the file however the network treats the packets in between.

The system consists of three programs, written in C with the POSIX API and the standard C library
only. They run on Linux and communicate over the loopback interface:

- The **sender** reads the file, splits it into packets of a configurable maximum payload, and runs
  one of three automatic repeat request (ARQ) protocols: Stop-and-Wait, Go-Back-N or Selective
  Repeat. It manages the sequence numbers, the send window and the retransmission timers, whose
  timeout it estimates from measured round-trip times (RTTs).
- The **receiver** checks every packet with a CRC-32, handles duplicate, old and out-of-order
  packets, acknowledges them, and writes the file.
- The **impairment emulator** sits between the two and relays every datagram in both directions. It
  models a full-duplex link, with a link rate, a queue and a propagation delay in each direction,
  and applies the impairments of a scenario file: jitter, data and ACK loss, corruption, duplication,
  reordering, and actions on individual packets. Its random decisions come from a seeded
  pseudo-random number generator, so the same scenario and seed always reproduce the same run.

```
            DATA                      DATA
  sender ---------->  emulator  ---------->  receiver
         <----------            <----------
             ACK                       ACK
```

The sender and the receiver never communicate directly. The protocol, the window size $W$, the
sequence-number width $k$, the maximum payload and every impairment parameter are set at run time,
on the command line or in the scenario file; the README lists them all.

Each program logs every event with a microsecond timestamp. After a run, the analysis scripts
compute the metrics from these logs: completion time, goodput, retransmission overhead, link
utilization, the RTT samples and the RTO over the course of the transfer, and the integrity of the
received file, checked by comparing SHA-256 hashes. Five experiments use these metrics to measure how
the window size, packet loss, propagation delay, the other impairments and the size of the
sequence-number space affect the three protocols, and compare the measurements with theory.

The rest of the report is organized as follows.

| Section | Content |
|---------|---------------------------------------------------------------------|
| 2       | Protocol design: packet format, the three ARQ protocols, the retransmission timeout, termination |
| 3       | Emulator design |
| 4       | Metrics and methodology |
| 5–9     | Experiments 1–5 |
| 10      | Failure conditions and limitations of the design |
| 11      | Division of work |
| 12      | AI use statement |
