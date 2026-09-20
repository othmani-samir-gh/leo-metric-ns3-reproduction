#!/usr/bin/env python3
"""
significance_test.py — LEGACY v1.0.0 ONLY.

DO NOT use this script for v1.1.0 release claims. It uses the historical
independent-group Welch workflow and does not implement the frozen R6 pairing,
scenario-cell inference, direct routeFingerprint evidence, or Holm family
correction. Use analysis/r6_statistical_reanalysis.py instead.

Historical description follows.

Test 4 of the pre-publication checklist: pairwise statistical
significance testing between metrics (LEO vs. hopcount, LEO vs. lqi,
LEO vs. lqi-literal, ...) for each (layout, envFactor) scenario, using
Welch's t-test (does not assume equal variance between groups -- the
right default when comparing routing metrics whose variance in energy
consumption is not guaranteed equal).

Requires: leo-results.csv (from leo-topologies.cc / run-experiments.sh).
Falls back to a pure-Python Welch's t-test implementation if scipy is
unavailable (no extra dependency required).

Usage:
    python3 significance_test.py leo-results.csv --out significance.csv
    python3 significance_test.py leo-results.csv --baseline leo --alpha 0.05
"""
import argparse
import csv
import math
from collections import defaultdict

try:
    from scipy import stats as _scipy_stats

    HAVE_SCIPY = True
except ImportError:
    HAVE_SCIPY = False


def _sample_var(x):
    n = len(x)
    m = sum(x) / n
    return sum((v - m) ** 2 for v in x) / (n - 1) if n > 1 else 0.0


def _reg_incomplete_beta(x, a, b, iters=200):
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    lbeta = math.lgamma(a) + math.lgamma(b) - math.lgamma(a + b)
    front = math.exp(math.log(x) * a + math.log(1 - x) * b - lbeta) / a
    f, c, d = 1.0, 1.0, 0.0
    for i in range(iters):
        m = i // 2
        if i == 0:
            numerator = 1.0
        elif i % 2 == 0:
            numerator = (m * (b - m) * x) / ((a + 2 * m - 1) * (a + 2 * m))
        else:
            numerator = -((a + m) * (a + b + m) * x) / ((a + 2 * m) * (a + 2 * m + 1))
        d = 1.0 + numerator * d
        if abs(d) < 1e-30:
            d = 1e-30
        d = 1.0 / d
        c = 1.0 + numerator / c
        if abs(c) < 1e-30:
            c = 1e-30
        f *= d * c
    return front * (f - 1.0)


def _student_t_cdf(t, df):
    """Numeric approximation of the Student's t CDF (used only when scipy
    is unavailable), accurate to ~1e-6 for typical df/t ranges here."""
    x = df / (df + t * t)
    ib = _reg_incomplete_beta(x, df / 2.0, 0.5)
    return 1.0 - 0.5 * ib


def welch_ttest(a, b):
    """Welch's t-test (two-sample, unequal variance). Returns (t, p, df)."""
    na, nb = len(a), len(b)
    ma, mb = sum(a) / na, sum(b) / nb
    va, vb = _sample_var(a), _sample_var(b)
    if HAVE_SCIPY:
        t, p = _scipy_stats.ttest_ind(a, b, equal_var=False)
        df = (va / na + vb / nb) ** 2 / ((va / na) ** 2 / (na - 1) + (vb / nb) ** 2 / (nb - 1))
        return t, p, df

    se = math.sqrt(va / na + vb / nb)
    if se == 0:
        return float("nan"), float("nan"), float("nan")
    t = (ma - mb) / se
    df = (va / na + vb / nb) ** 2 / ((va / na) ** 2 / (na - 1) + (vb / nb) ** 2 / (nb - 1))
    p = 2.0 * (1.0 - _student_t_cdf(abs(t), df))
    return t, p, df


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_path")
    ap.add_argument("--out", default="significance.csv")
    ap.add_argument("--baseline", default="leo", help="metric to compare every other metric against")
    ap.add_argument("--alpha", type=float, default=0.05, help="significance threshold")
    ap.add_argument("--only-discriminating", metavar="DISCRIMINATION_CSV",
                    help="path to discrimination.csv (from discrimination_check.py); restricts the "
                         "analysis to scenario cells where the metrics actually chose different "
                         "routes. Cells where metrics coincide carry no information about metric "
                         "quality and including them dilutes/biases the result.")
    ap.add_argument("--column", default="totalEnergyMWs",
                    help="which numeric column to compare (e.g. totalEnergyMWs, pingTimeouts, "
                         "pingNoRoute, energy_pathDiscovery). LEO claims BOTH lower energy and "
                         "higher reliability, so both should be tested before drawing a conclusion.")
    args = ap.parse_args()

    # Optional: load the set of discriminating (layout, envFactor, interference) cells.
    discriminating = None
    if args.only_discriminating:
        discriminating = set()
        with open(args.only_discriminating, newline="") as f:
            for r in csv.DictReader(f):
                if r["verdict"] == "DISCRIMINATING":
                    discriminating.add((r["layout"], r["envFactor"], r["interference"]))
        print(f"Restricting to {len(discriminating)} discriminating scenario cells "
              f"(from {args.only_discriminating}).")

    if not HAVE_SCIPY:
        print("NOTE: scipy not found, using a built-in Welch's t-test approximation "
              "(accurate to ~1e-6). Install scipy for the reference implementation if "
              "you want exact reproducibility with common statistics packages.")

    groups = defaultdict(lambda: defaultdict(list))
    skipped_cells = set()
    with open(args.csv_path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            cell = (r["layout"], r["envFactor"], r.get("interference", "0"))
            if discriminating is not None and cell not in discriminating:
                skipped_cells.add(cell)
                continue
            key = (r["layout"], r["envFactor"])
            groups[key][r["metric"]].append(float(r[args.column]))

    if skipped_cells:
        print(f"Skipped {len(skipped_cells)} degenerate cells (metrics chose identical routes).")

    rows = []
    for (layout, env), by_metric in sorted(groups.items()):
        if args.baseline not in by_metric:
            continue
        base = by_metric[args.baseline]
        for metric, values in sorted(by_metric.items()):
            if metric == args.baseline:
                continue
            if len(values) < 2 or len(base) < 2:
                rows.append((layout, env, metric, len(base), len(values), "nan", "nan", "nan", "insufficient_n"))
                continue
            t, p, df = welch_ttest(base, values)
            mean_base = sum(base) / len(base)
            mean_other = sum(values) / len(values)
            pct_diff = 100.0 * (mean_base - mean_other) / mean_other if mean_other else float("nan")
            sig = "significant" if (isinstance(p, float) and p == p and p < args.alpha) else "not_significant"
            direction = f"{args.baseline}_lower" if mean_base < mean_other else f"{args.baseline}_higher"
            rows.append((layout, env, metric, len(base), len(values), round(t, 4), round(p, 6), round(pct_diff, 2),
                         f"{sig} ({direction})"))

    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["layout", "envFactor", "comparedMetric", f"n_{args.baseline}", "n_compared",
                    "t_statistic", "p_value", f"pctDiff_{args.baseline}_vs_compared",
                    f"verdict_on_{args.column}"])
        w.writerows(rows)

    print(f"Wrote {args.out} ({len(rows)} comparisons, baseline={args.baseline}, alpha={args.alpha})")
    n_sig = sum(1 for r in rows if isinstance(r[-1], str) and r[-1].startswith("significant"))
    print(f"{n_sig}/{len(rows)} comparisons reached significance at alpha={args.alpha}")

    # Directional breakdown of the significant results -- the number that
    # actually answers "does the baseline win?", which a bare count does not.
    sig_lower = sum(1 for r in rows if isinstance(r[-1], str) and r[-1].startswith("significant")
                    and f"{args.baseline}_lower" in r[-1])
    sig_higher = sum(1 for r in rows if isinstance(r[-1], str) and r[-1].startswith("significant")
                     and f"{args.baseline}_higher" in r[-1])
    print(f"\nOf the {n_sig} significant results:")
    print(f"  {sig_lower} favour {args.baseline} (lower energy)")
    print(f"  {sig_higher} favour the compared metric ({args.baseline} used MORE energy)")
    if n_sig and sig_higher > sig_lower:
        print(f"\nNOTE: the significant results predominantly do NOT favour {args.baseline} on "
              f"total energy. Before interpreting this, check whether {args.baseline} trades "
              f"energy for reliability (compare pingTimeouts/pingNoRoute), and whether the extra "
              f"energy is concentrated in path discovery (energy_pathDiscovery) -- the source "
              f"paper itself predicts higher path-discovery cost for LEO.")


if __name__ == "__main__":
    main()
