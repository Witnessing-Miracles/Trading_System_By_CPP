# Low-Latency C++ Trading System: T1–T12 Hop Latency Measurement Report

**Measurement date:** 2026-08-16
**Target:** `exchange_main` and `trading_main` from the Exchange_with_Client project, plus a single-message integrated run for causal cross-validation.

**Scope note:** This report characterizes data flow, queueing paths, and scheduling jitter within the system. It is **not** a production HFT latency benchmark — the test environment has no core pinning, no real-time scheduling, no NIC/kernel bypass, no CPU frequency locking, and the logging itself sits on the hot path.

---

## 1. Build, Run, and Test Scope

| Item | Configuration |
|---|---|
| OS | Ubuntu 24.04 sandbox |
| Toolchain | CMake 3.28.3, Ninja 1.11.1, GCC/G++ 13.3.0, C++20 |
| Build mode | Release |
| Network topology | Same-host loopback: TCP `127.0.0.1:12345`; UDP multicast `233.252.14.1:20000` and `233.252.14.3:20001` |
| Workload | 1 `RANDOM` client, 1 ticker; 24 order-receipt events successfully recorded |
| Statistics method | Adjacent T-tag pairing per the repository's own performance-analysis notebook; small sample retains all positive samples (no 1%–99% trimming) |

The default demo script launches a `MAKER` client against an empty order book, which produces no trade flow and therefore no T1–T12 timestamps — this is expected behavior for an idle exchange, not a build failure. Switching to a `RANDOM` client produces real order flow, which is the basis for the measurements below.

---

## 2. Test Fix Applied

Running the original binaries as-is with `RANDOM` produced client-side timestamps (T10–T12) but no exchange-side timestamps (T1+). Root cause: the non-blocking TCP send path did not handle `EAGAIN`/`EINPROGRESS`/partial writes correctly and silently dropped order frames that hadn't actually reached the kernel.

To close the measurement loop end-to-end, a separate copy of the project was used with two minimal fixes to send/connect semantics (no changes to core matching, queueing, or network logic), and pre-allocated capacity was reduced to fit the sandbox's memory limits. This produced a full T1–T12 log chain across 24 samples.

| Run | Result | Use |
|---|---|---|
| Original script + MAKER | Processes start correctly, but no order/market flow, TTT=0 | Confirms default demo load doesn't generate hop data |
| Original binary + RANDOM | T10–T12 only; client TCP send returns -1 | Isolated the non-blocking write bug |
| Fixed copy + RANDOM (real binaries) | Full T1–T9 (MD and ACK branches) | **Primary statistics** |
| Fixed copy + single-message driver | One sample per tag, fully closed loop | Causal ordering + total path latency |

---

## 3. T1–T12 Path Definition

Market data and order acknowledgements diverge into two branches after the matching engine:

```
Order submission:  TradeEngine T10 → OrderGateway T11 → TCP write T12
                                          │
                                          ▼
Order receipt:     OrderServer T1 → FIFO queue T2 → MatchingEngine T3
                                          ├─ Market data: T4 → T5 → T6 —UDP—→ T7 → T8 → T9
                                          └─ Order ack:   T4t → T5t → T6t —TCP—→ T7t → T8t → T9t
```

---

## 4. Aggregate Hop Statistics (24 samples, `exchange_main` + `trading_main`)

All units in µs. P50 is a more reliable indicator of steady-state hot-path latency in a non-isolated environment; P95/mean expose scheduling and I/O tail effects.

| Hop | Path | n | P50 | Mean | P95 | Notes |
|---|---|---|---|---|---|---|
| T1 | T1→T2 | 24 | 38.3 | 39.5 | 43.0 | OrderServer receipt → FIFO publish |
| T2 | T2→T3 | 24 | 1.8 | 1.7 | 2.9 | FIFO → matching thread read |
| T3-MD | T3→T4 | 19 | 59.2 | 62.4 | 85.1 | Matching engine → market data publish |
| T3-ACK | T3→T4t | 24 | 44.0 | 48.9 | 74.7 | Matching engine → order ack publish |
| T4-MD → T8-MD | market data branch | 18–22 | single-digit to tens of µs | up to several ms | up to ~12 ms | P50 stays low; mean/P95 inflated by non-pinned consumer thread scheduling delay |
| T4-ACK → T8-ACK | order ack branch | 24–28 | single-digit to tens of µs | up to several hundred µs | up to ~10 ms | Same pattern as above |
| T10→T11 | strategy queue → gateway | 24 | ~1.0 | ~2,378 | ~7,324 | Gateway thread scheduling tail dominates |
| T11→T12 | gateway read → TCP write | 24 | ~20.6 | ~20.6 | ~25.6 | Serialization + socket send path |
| T12→T1 | TCP write → exchange receipt | 24 | ~36.4 | ~1,393 | ~9,759 | Loopback TCP + scheduling, includes a ~10.8 ms outlier |

**How to read this:** most median (P50) latencies fall in the 0.5–59 µs range and represent genuine hot-path cost. Several hops show mean values several orders of magnitude above their own median — this is caused entirely by scheduling delay on threads that aren't core-pinned or busy-polling, not by compute cost. Mean/max should not be treated as steady-state latency figures in this environment.

The market data branch (T6→T7) shows fewer completed pairs (18) than sent tags (22), consistent with UDP multicast having no retransmission and no snapshot-recovery active in this run — this is an expected property of the transport, not evidence of the missing samples being zero-latency.

*(See `hop_statistics.png` for the full per-hop chart, and `hop_statistics.csv` for exact sample counts, P50/mean/P95/min/max.)*

---

## 5. Single-Message Causal Validation

To remove ambiguity from multi-event tag pairing, a single-message driver was run using the same production components (`MatchingEngine`, `OrderServer`, `MarketDataPublisher`, `OrderGateway`, `MarketDataConsumer`, `TradeEngine`), with each tag emitted exactly once, allowing an exact hop-by-hop causal breakdown.

| Full path | Total | Dominant contributor |
|---|---|---|
| Order submit → gateway → TCP → exchange receipt → matching → ack | 6.489 ms | T8t→T9t: 6.213 ms |
| Order submit → gateway → TCP → exchange receipt → matching → market data | 6.533 ms | T8→T9: 6.170 ms |

These totals do **not** mean "the system takes 6.5 ms to process an order" — they reflect ordinary Linux scheduling delay on independently-woken consumer threads in this environment. What they do confirm is the system's real end-to-end constraint: consumer threads are not core-isolated busy-poll loops, and file logging plus generic Linux sockets and timer-driven scheduling are all present on the measured path.

*(See `hop_latencies.png` for the per-hop breakdown of this single-message run.)*

---

## 6. Engineering Takeaways

The codebase's component split — MatchingEngine, OrderServer, MarketDataPublisher, OrderGateway, MarketDataConsumer, TradeEngine, each with a clearly scoped hot-path responsibility and lock-free queue boundary — demonstrates a working single-writer/single-thread component model for a low-latency system. Pushing this architecture toward production-grade latency requirements would require turning each single-threaded component into a pinned, isolated, observable, and recoverable shard.

| Validated in this run | Still needed for production-grade latency |
|---|---|
| Lock-free queues decouple components; matching, ack, and market-data paths all function end-to-end | Pin critical threads (`sched_setaffinity`), lock CPU governor to performance mode, isolate housekeeping interrupts |
| T1–T12 (and `t`-suffixed ack branch) hop tags allow queue- and network-boundary attribution | Robust non-blocking connection state machine, EPOLLOUT handling, partial-write buffering, backpressure, reconnect logic |
| Same-host loopback measurement distinguishes steady-state P50 from multi-millisecond scheduling tail | Production NIC, hardware timestamping or a unified clock domain, UDP busy-poll/kernel bypass, adequate warm-up |
| Fixed-capacity data structures avoid dynamic allocation on the hot path | Capacity sized per partition/shard on real hardware with page pre-faulting; the original 1M-entry fixed allocation does not fit small containers as-is |

---

## 7. Reproducibility

| File | Contents |
|---|---|
| `ttt_main_run_hop_analysis/hop_statistics.csv` | Per-hop sample count, mean, P50, P95, min, max for `exchange_main` + `trading_main` |
| `ttt_main_run_hop_analysis/hop_statistics.png` | Aggregate multi-event hop chart |
| `ttt_hop_analysis/hop_latencies.csv` | Single-message causal per-hop breakdown |
| `ttt_hop_analysis/hop_latencies.png` | Single-message hop chart |
| `ttt_measurement_raw_logs.zip` | Raw measurement logs and analysis output |
| `analyze_main_run_hops.py`, `analyze_ttt_hops.py` | Scripts used to generate the above |

Equivalent to running:
```bash
./cmake-build-release/exchange_main
./cmake-build-release/trading_main 1 RANDOM
```

The original repository script (`bash scripts/run_exchange_and_clients.sh`) defaults to a `MAKER` client, which only demonstrates strategy behavior against existing market data. To reproduce end-to-end order hops, use `RANDOM` explicitly and apply the non-blocking send fix described in Section 2.

---

## References

1. Repository performance-analysis notebook — TTT parsing and hop definitions
2. Repository TCP socket implementation
3. Repository fixed-capacity type constants