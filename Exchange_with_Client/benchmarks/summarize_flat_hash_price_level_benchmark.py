#!/usr/bin/env python3
"""Summarize repeated flat-hash A/B output and plot latency.

Run the benchmark 5 times into one tee log, then:
  python3 summarize_flat_hash_price_level_benchmark.py --input /tmp/flat_hash_comparison.txt
"""
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import median

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

LEVELS = [16, 64, 128, 256]
BLUE = "#176B87"
ORANGE = "#D97706"


def parse(path: Path):
    data = defaultdict(list)
    with path.open(encoding="utf-8", errors="replace", newline="") as source:
        for row in csv.reader(source):
            if len(row) != 8 or row[0] not in ("BUY", "SELL"):
                continue
            side, levels = row[0], int(row[1])
            values = {
                ("unordered_map", "hit"): row[2],
                ("flat_hash", "hit"): row[3],
                ("unordered_map", "miss"): row[4],
                ("flat_hash", "miss"): row[5],
                ("unordered_map", "insert_erase"): row[6],
                ("flat_hash", "insert_erase"): row[7],
            }
            for key, value in values.items():
                data[(key[0], side, levels, key[1])].append(float(value))
    if not data:
        raise ValueError(f"No comparison CSV rows found in {path}")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--outdir", type=Path, default=Path("flat-hash-summary"))
    args = parser.parse_args()
    data = parse(args.input)
    summary = {key: (median(vals), min(vals), max(vals), len(vals)) for key, vals in data.items()}
    args.outdir.mkdir(parents=True, exist_ok=True)

    csv_path = args.outdir / "flat_hash_latency_summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(["implementation", "side", "levels", "operation", "median_ns_per_op",
                         "min_ns_per_op", "max_ns_per_op", "repetitions"])
        for key in sorted(summary):
            writer.writerow([*key, *(f"{v:.4f}" if isinstance(v, float) else v for v in summary[key])])

    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.titlesize": 12, "legend.fontsize": 9})
    fig, axes = plt.subplots(1, 2, figsize=(12, 5.5))
    fig.suptitle("Price Index Latency Comparison", fontsize=16, fontweight="bold", y=0.98)
    fig.text(0.5, 0.93, "Median across runs; whiskers show min–max; lower is faster",
             ha="center", fontsize=10, color="#475569")

    lookup_ax, update_ax = axes
    for implementation, color in (("unordered_map", BLUE), ("flat_hash", ORANGE)):
        for operation, style, marker in (("hit", "-", "o"), ("miss", "--", "s")):
            points = []
            for levels in LEVELS:
                samples = [v for side in ("BUY", "SELL")
                           for v in data[(implementation, side, levels, operation)]]
                points.append((median(samples), min(samples), max(samples)))
            medians = [p[0] for p in points]
            lows = [p[0] - p[1] for p in points]
            highs = [p[2] - p[0] for p in points]
            lookup_ax.errorbar(LEVELS, medians, yerr=[lows, highs], color=color, linestyle=style,
                               marker=marker, linewidth=2, capsize=3,
                               label=f"{implementation} {operation}")
    lookup_ax.set_title("Lookup")
    lookup_ax.set_xlabel("Active price levels")
    lookup_ax.set_ylabel("Nanoseconds per lookup")
    lookup_ax.set_xticks(LEVELS)
    lookup_ax.legend(ncol=2)

    for side, style, marker in (("BUY", "-", "o"), ("SELL", "--", "s")):
        for implementation, color in (("unordered_map", BLUE), ("flat_hash", ORANGE)):
            points = [summary[(implementation, side, levels, "insert_erase")] for levels in LEVELS]
            medians = [p[0] for p in points]
            lows = [p[0] - p[1] for p in points]
            highs = [p[2] - p[0] for p in points]
            update_ax.errorbar(LEVELS, medians, yerr=[lows, highs], color=color, linestyle=style,
                               marker=marker, linewidth=2, capsize=3,
                               label=f"{implementation} {side}")
    update_ax.set_title("Insert + erase")
    update_ax.set_xlabel("Active price levels")
    update_ax.set_ylabel("Nanoseconds per operation\n(insert and erase counted separately)")
    update_ax.set_xticks(LEVELS)
    update_ax.legend(ncol=2)

    fig.text(0.5, 0.02, "Synthetic price-index workload; not a full order-book benchmark.",
             ha="center", fontsize=9, color="#475569")
    fig.tight_layout(rect=(0.02, 0.07, 0.98, 0.88))
    png_path = args.outdir / "flat_hash_latency.png"
    fig.savefig(png_path, dpi=180, bbox_inches="tight")
    print(f"CSV: {csv_path}")
    print(f"PNG: {png_path}")
    print(f"External repetitions parsed: {min(len(v) for v in data.values())}")


if __name__ == "__main__":
    main()
