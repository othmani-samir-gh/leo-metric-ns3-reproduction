#!/usr/bin/env python3
"""
analyze-results.py — LEGACY v1.0.0 ONLY.

Retained for historical provenance. It is not the authoritative v1.1.0
analysis path. Use analysis/r6_statistical_reanalysis.py and the checksum-bound
R5/R6 artifacts for current results.

Historical description follows.

Post-processes leo-results.csv (produced by run-experiments.sh) into the
same kind of summary the paper reports in Figs. 5-7:
  - mean total network energy (mWs) per (layout, envFactor, metric),
    with a 95% CI across the RUNS repetitions, using a t-distribution
    critical value (not a fixed z=1.96) since typical run counts (6-20)
    are well below the n>=30 threshold where that approximation holds.
  - mean ping outcome counts (pingTimeouts, pingNoRoute, pingSent) per cell.
  - Fig. 7-style ratio of each metric's mean energy to the mean-of-all-
    metrics for that (layout, envFactor) scenario, with Best/Average/Worst
    reachability buckets assigned by envFactor terciles per layout.
  - an integrity check: if any (layout, metric) has both interference=0
    and interference=1 rows at the same envFactor with statistically
    indistinguishable (or literally identical) energy, this is flagged --
    a symptom of the interference variant not actually being wired up
    (the bug this check exists to catch, see README's P0-3 fix history).

Usage:
    python3 analyze-results.py leo-results.csv --out summary.csv
"""
import argparse
import csv
import math
from collections import defaultdict

# Two-tailed 95% critical t-values for small degrees of freedom (df = n-1).
# For df beyond this table, falls back to the z=1.96 large-sample value
# (the difference is under 1% once df >= 30).
_T_TABLE_95 = {
    1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571,
    6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
    11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131,
    16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086,
    24: 2.064, 29: 2.045,
}


def t_critical_95(df):
    if df <= 0:
        return float("nan")
    if df in _T_TABLE_95:
        return _T_TABLE_95[df]
    candidates = [k for k in _T_TABLE_95 if k >= df]
    if candidates:
        return _T_TABLE_95[min(candidates)]
    return 1.96


def mean_ci95(values):
    n = len(values)
    if n == 0:
        return float("nan"), float("nan"), 0
    m = sum(values) / n
    if n < 2:
        return m, 0.0, n
    var = sum((v - m) ** 2 for v in values) / (n - 1)
    sd = math.sqrt(var)
    tcrit = t_critical_95(n - 1)
    ci = tcrit * sd / math.sqrt(n)
    return m, ci, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("--out", default="summary.csv")
    ap.add_argument("--ratio-out", default="summary_ratios.csv")
    args = ap.parse_args()

    rows = defaultdict(list)
    with open(args.csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            key = (r["layout"], r["envFactor"], r["metric"], r["interference"])
            rows[key].append(r)

    cell_energy_mean = {}
    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(
            [
                "layout", "envFactor", "metric", "interference", "n",
                "meanEnergyMWs", "ci95EnergyMWs",
                "meanTimeouts", "ci95Timeouts",
                "meanNoRoute", "ci95NoRoute",
                "meanSent", "ci95Sent",
            ]
        )
        for key, recs in sorted(rows.items()):
            energies = [float(r["totalEnergyMWs"]) for r in recs]
            timeouts = [float(r["pingTimeouts"]) for r in recs]
            noroute = [float(r.get("pingNoRoute", 0.0)) for r in recs]
            sent = [float(r["pingSent"]) for r in recs]
            mE, ciE, n = mean_ci95(energies)
            mT, ciT, _ = mean_ci95(timeouts)
            mN, ciN, _ = mean_ci95(noroute)
            mS, ciS, _ = mean_ci95(sent)
            w.writerow([*key, n, mE, ciE, mT, ciT, mN, ciN, mS, ciS])
            cell_energy_mean[key] = mE

    print(f"Wrote {args.out}")

    scenario_metrics = defaultdict(dict)
    for (layout, env, metric, interference), mE in cell_energy_mean.items():
        scenario_metrics[(layout, env, interference)][metric] = mE

    layout_envfactors = defaultdict(set)
    for (layout, env, _interference) in scenario_metrics:
        layout_envfactors[layout].add(float(env))

    bucket_by_layout_env = {}
    for layout, envs in layout_envfactors.items():
        sorted_envs = sorted(envs)
        n = len(sorted_envs)
        for idx, env in enumerate(sorted_envs):
            if n <= 1:
                bucket = "Average"
            elif idx < n / 3:
                bucket = "Best"
            elif idx < 2 * n / 3:
                bucket = "Average"
            else:
                bucket = "Worst"
            bucket_by_layout_env[(layout, env)] = bucket

    with open(args.ratio_out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["layout", "envFactor", "interference", "reachabilityBucket", "metric", "ratioToMeanPct"])
        for (layout, env, interference), metrics in sorted(scenario_metrics.items()):
            if not metrics:
                continue
            mean_of_metrics = sum(metrics.values()) / len(metrics)
            bucket = bucket_by_layout_env.get((layout, float(env)), "Average")
            for metric, mE in sorted(metrics.items()):
                ratio_pct = 100.0 * mE / mean_of_metrics if mean_of_metrics else float("nan")
                w.writerow([layout, env, interference, bucket, metric, f"{ratio_pct:.1f}"])

    print(f"Wrote {args.ratio_out}")

    warned = False
    for (layout, env, metric, interference0) in list(cell_energy_mean):
        if interference0 != "0":
            continue
        key1 = (layout, env, metric, "1")
        if key1 not in cell_energy_mean:
            continue
        e0 = cell_energy_mean[(layout, env, metric, "0")]
        e1 = cell_energy_mean[key1]
        if e0 == 0:
            continue
        rel_diff = abs(e1 - e0) / abs(e0)
        if rel_diff < 0.01:
            print(
                f"WARNING: layout={layout} envFactor={env} metric={metric}: "
                f"interference=0 mean={e0:.3g} vs interference=1 mean={e1:.3g} "
                f"(relative difference {rel_diff*100:.2f}%). This is suspiciously "
                f"small for a scenario that is supposed to degrade hypotenuse-node "
                f"links -- check that --interference=true is actually being passed "
                f"and that WsnChannel::SetLinkSnrPenaltyDb is being invoked (see "
                f"README's P0-3 fix)."
            )
            warned = True
    if not warned:
        print("Integrity check: no interference=0/1 pairs looked suspiciously identical.")


if __name__ == "__main__":
    main()
