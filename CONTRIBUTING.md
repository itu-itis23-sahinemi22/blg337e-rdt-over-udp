# Contributing

BLG 337E Project 1: reliable data transfer over UDP (Stop-and-Wait, Go-Back-N, Selective Repeat) with an impairment emulator.
Assignment: [BLG337E_Project_1.pdf](BLG337E_Project_1.pdf).

## Team

| Who | Owns |
|---|---|
| Emir (`itu-itis23-sahinemi22`) | Protocol: sender, receiver, SW/GBN/SR, RTO, CRC-32 |
| Yakup | Emulator, scenario files, run scripts, analysis and plots |

The two sides meet only at the **packet header** and the **log format**. Both are fixed in M1; after that we work in parallel.

## How we work

1. **Every task is an issue, and every issue has exactly one owner.** The other person reviews its PR.
2. **No direct pushes to `main`.** One branch per issue: `<issue-no>-short-name` (e.g. `12-selective-repeat`).
3. **Put `Closes #<no>` in the PR description.** Merge only after the other person approves. We squash-merge.
4. **No merging while CI is red** (CI arrives with #7).
5. **Changing the packet header or the log format:** open an issue first, both approve, then change the code.
6. **We work asynchronously.** Post progress, blockers and decisions as comments on the issue or PR, so the other person can catch up at any time.

```sh
git switch main && git pull
git switch -c 12-selective-repeat
# ... work, commit ...
git push -u origin 12-selective-repeat
gh pr create --fill                        # add "Closes #12" to the description
gh pr review <pr-no> --approve             # the other person
gh pr merge <pr-no> --squash --delete-branch
```

## Opening an issue

- **Title:** the task, in a few words.
- **Body:** `What` (what to do), `Done when` (acceptance criteria), `Depends on` (#no) if any.
- **Labels:** owner (`emir` / `yakup`) + area (`protocol`, `emulator`, `common`, `analysis`, `experiment`, `ci`, `documentation`).
- **Milestone:** the phase it belongs to. **Assignee:** the owner.
- **Bugs:** `bug` label, plus how to reproduce it (command, seed, scenario file).

```sh
gh issue create --title "GBN: window resent twice on timeout" \
  --label "bug,protocol,emir" --milestone "M3 · Full protocol + emulator" --assignee @me
```

## Environment

- C with the POSIX API on Linux, standard C library only. On Windows we use **WSL Ubuntu**.
- Clone the repo inside WSL (`~/blg337e-rdt-over-udp`), not in a OneDrive folder.
- Needed: `gcc`, `make`, `python3` (for the analysis).

## Plan

Milestones are done in order; a milestone is finished when all of its issues are closed.

| Milestone | Emir | Yakup |
|---|---|---|
| M1 · Interfaces | Packet header + CLI spec, repo skeleton, CRC-32 | Log and scenario format, PRNG/time/log helpers |
| M2 · Stop-and-Wait end-to-end | SW + FIN (first without the emulator), CI | Emulator core, dummy traffic generator, `run_one.sh` |
| M3 · Full protocol + emulator | GBN, SR, RTO | All impairments, scenario file, per-packet actions |
| M4 · Analysis | Robustness testing, `run_all.sh` | Log parser, metrics, plots |
| M5 · Experiments | Experiments 2, 3, 5 | Experiments 1, 4 |
| M6 · Submission | Protocol spec, report (design, limitations) | State diagrams, README, report (emulator, metrics, division of work, AI statement), submission check |

- **First integration at the end of M2:** real sender → emulator → receiver, and the SHA-256 hashes must match.
- **Each experiment issue includes its own report section:** runs, plots, mathematical derivation, interpretation.
- **Cross-learning:** Yakup draws the state diagrams. In the demo, Emir presents the emulator and Yakup presents the protocol. Both of us must be able to explain every part.

### Initial technical decisions (finalized in M1)

- **Header (12 bytes, network byte order):** `type u8 | flags u8 | len u16 | seq/ack u32 | crc32 u32`.
  - The RETX bit in `flags` is for logging/analysis only; it lets the utilization metric separate new packets from retransmissions.
- **Timestamps:** `CLOCK_MONOTONIC`, microseconds. All three programs use the same clock, so their logs can be merged.
- **Program structure:** all three programs are single-threaded, with a `poll()` event loop.
- **RTO:** RFC 6298 (α=1/8, β=1/4, K=4) + Karn + exponential backoff. We choose and justify the minimum RTO ourselves.
- **Emulator determinism:** a separate PRNG stream per direction. Every packet draws a fixed number of random values, whatever the outcome.
