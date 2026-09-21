#!/usr/bin/env python3
"""
discrimination_check.py — LEGACY v1.0.0 ONLY.

WARNING: this historical diagnostic uses energy equality as a proxy for route
identity and recommends outcome-dependent exclusion. Both practices are
forbidden for v1.1.0 inference. Direct routeFingerprint evidence is now
available; use analysis/r6_statistical_reanalysis.py.

Historical description follows.

Diagnostic for a critical validity question: do the compared metrics
actually select DIFFERENT routes in a given scenario, or do they
coincide? If LEO and Hop-count produce bit-identical total energy for a
given run, they chose identical paths, and any statistical comparison in
that scenario is measuring nothing -- it cannot support a claim about
either metric's performance.

Reports, per (layout, envFactor, interference) cell, the fraction of runs
in which LEO and Hop-count produced identical energy. Cells at or near
100% carry no information about metric quality and should be either
excluded from the headline comparison (with an explicit statement of why)
or redesigned to create genuine link-quality variance.

Usage:
    python3 discrimination_check.py leo-results.csv
    python3 discrimination_check.py leo-results.csv --threshold 50 --out discrimination.csv
"""
import argparse
import csv
from collections import defaultdict


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("--baseline", default="leo")
    ap.add_argument("--against", default="hopcount")
    ap.add_argument("--threshold", type=float, default=50.0,
                    help="cells with identical-rate BELOW this %% are considered discriminating")
    ap.add_argument("--out", default="discrimination.csv")
    args = ap.parse_args()

    per_run = defaultdict(dict)
    with open(args.csv_path, newline="") as f:
        for r in csv.DictReader(f):
            key = (r["layout"], r["envFactor"], r.get("interference", "0"), r["run"])
            per_run[key][r["metric"]] = float(r["totalEnergyMWs"])

    agg = defaultdict(lambda: [0, 0])  # (layout, env, intf) -> [identical, total]
    for (lay, env, intf, _run), by_metric in per_run.items():
        if args.baseline in by_metric and args.against in by_metric:
            cell = (lay, env, intf)
            agg[cell][1] += 1
            if abs(by_metric[args.baseline] - by_metric[args.against]) < 1e-9:
                agg[cell][0] += 1

    rows = []
    discriminating = []
    degenerate = []
    for cell in sorted(agg):
        same, tot = agg[cell]
        pct = 100.0 * same / tot if tot else float("nan")
        verdict = "DISCRIMINATING" if pct < args.threshold else "degenerate"
        rows.append((*cell, same, tot, round(pct, 1), verdict))
        (discriminating if pct < args.threshold else degenerate).append((cell, pct))

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["layout", "envFactor", "interference", "identicalRuns", "totalRuns",
                    "identicalPct", "verdict"])
        w.writerows(rows)

    print(f"Comparing {args.baseline} vs {args.against}\n")
    print(f"{'DISCRIMINATING cells':<40} (metrics chose different routes)")
    print("-" * 70)
    for cell, pct in sorted(discriminating, key=lambda x: x[1]):
        print(f"  layout={cell[0]:<12} env={cell[1]:<7} intf={cell[2]}  identical={pct:.0f}%")
    if not discriminating:
        print("  (none)")

    print(f"\n{'DEGENERATE cells':<40} (metrics chose the SAME routes -- no information)")
    print("-" * 70)
    for cell, pct in sorted(degenerate, key=lambda x: -x[1]):
        print(f"  layout={cell[0]:<12} env={cell[1]:<7} intf={cell[2]}  identical={pct:.0f}%")
    if not degenerate:
        print("  (none)")

    n_d, n_total = len(discriminating), len(agg)
    print(f"\nSUMMARY: {n_d}/{n_total} scenario cells discriminate between "
          f"{args.baseline} and {args.against} (threshold: identical < {args.threshold:.0f}%).")
    print(f"Wrote {args.out}")
    if n_d == 0:
        print("\nWARNING: no scenario discriminates. Any energy comparison from this "
              "dataset is measuring identical routing decisions and cannot support a "
              "performance claim about either metric.")


if __name__ == "__main__":
    main()
