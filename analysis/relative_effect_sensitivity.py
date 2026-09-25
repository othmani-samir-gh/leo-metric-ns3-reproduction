#!/usr/bin/env python3
"""
relative_effect_sensitivity.py — sensitivity analysis for the primary estimand.

The primary estimand (R6) is the equal-cell mean of per-cell ABSOLUTE paired
differences, divided by the equal-cell baseline mean. Cell energies span about
an order of magnitude (hop-count cell means 10.3-110.7 mWs at cap 1), so
high-energy cells dominate that estimand: equal weighting in mWs is not equal
weighting of relative effects.

This recomputes every primary comparison with each cell contributing its own
RELATIVE effect with equal weight:

  per-cell effect r_c:
    energy  : log( mean_LEO / mean_baseline )
    counts  : log( (sum_LEO + 0.5) / (sum_baseline + 0.5) )   # Haldane correction;
              keeps cells whose baseline count is zero instead of excluding them
  stratum effect  : exp( mean_c r_c ) - 1      (geometric-mean ratio, as %)
  test            : two-sided cell-level sign-flip on r_c
                    (exact for 5 cells, 200,000 Monte Carlo draws for 30)
  interval        : 95% percentile, hierarchical bootstrap (cells, then paired
                    runs within cell), 20,000 replicates
  multiplicity    : Holm within the same families as R6
                    (one baseline x one outcome, across the six strata)

For energy it also reports the plain equal-cell mean of per-cell percentage
differences as a second relative formulation.

Usage, from the repository root:
    python3 analysis/relative_effect_sensitivity.py --out results/v1.1.1/relative_effect_sensitivity.csv
"""
import argparse, csv, glob, hashlib, itertools
from collections import defaultdict
import numpy as np

PRI = 'results/v1.1.0/configs/primary_static_source_constrained'
SEED = 20260920

def stable_seed(*parts):
    # deterministic across processes and machines (built-in hash() is randomized)
    return int.from_bytes(hashlib.sha256('|'.join(map(str, (SEED,) + parts)).encode()).digest()[:8], 'big')

def load():
    data = defaultdict(dict)
    for d in glob.glob(f'{PRI}/*'):
        for r in csv.DictReader(open(f'{d}/main.csv')):
            data[(r['layout'], r['envFactor'], r['interference'], r['relayCap'], r['metric'])][int(r['run'])] = r
    return data

def cell_arrays(data, intf, cap, comp, col):
    out = []
    for k in sorted(data):
        lay, env, i, c, m = k
        if i == intf and c == cap and m == 'leo':
            L = data[k]; C = data[(lay, env, i, c, comp)]
            runs = sorted(set(L) & set(C))
            out.append((np.array([float(L[r][col]) for r in runs]),
                        np.array([float(C[r][col]) for r in runs])))
    return out  # list of (L_runs, C_runs); all cells have 20 runs

def r_cell(L, C, count):
    if count:
        return np.log((L.sum() + 0.5) / (C.sum() + 0.5))
    return np.log(L.mean() / C.mean())

def sign_flip(x, rng):
    x = np.asarray(x); n = len(x); obs = abs(x.mean())
    if n <= 16:
        signs = np.array(list(itertools.product((1, -1), repeat=n)))
        return float(np.mean(np.abs((signs * x).mean(axis=1)) >= obs - 1e-12)), 'exact'
    s = rng.choice((1, -1), size=(200000, n))
    return float((np.sum(np.abs((s * x).mean(axis=1)) >= obs - 1e-12) + 1) / 200001), 'monte_carlo'

def bootstrap(cells, count, rng, reps=20000, chunk=1000):
    L = np.stack([c[0] for c in cells]); C = np.stack([c[1] for c in cells])
    nc, nr = L.shape; vals = []
    for start in range(0, reps, chunk):
        b = min(chunk, reps - start)
        ci = rng.integers(0, nc, (b, nc)); ri = rng.integers(0, nr, (b, nc, nr))
        Ls = L[ci[:, :, None], ri]; Cs = C[ci[:, :, None], ri]   # paired: same run index for both
        if count:
            r = np.log((Ls.sum(2) + 0.5) / (Cs.sum(2) + 0.5))
        else:
            r = np.log(Ls.mean(2) / Cs.mean(2))
        vals.append(np.exp(r.mean(1)) - 1)
    return np.quantile(np.concatenate(vals), [0.025, 0.975]) * 100

def holm(ps):
    order = np.argsort(ps); m = len(ps); adj = np.empty(m); run = 0.0
    for rank, i in enumerate(order):
        run = max(run, min(1.0, (m - rank) * ps[i])); adj[i] = run
    return adj

ap = argparse.ArgumentParser()
ap.add_argument('--out', default='results/v1.1.1/relative_effect_sensitivity.csv')
ap.add_argument('--reps', type=int, default=20000)
a = ap.parse_args()

data = load()
rows = []
for comp in ['hopcount', 'lqi']:
    for col in ['totalEnergyMWs', 'pingTimeouts', 'pingNoRoute']:
        count = col != 'totalEnergyMWs'
        fam = []
        for intf in ['0', '1']:
            for cap in ['1', '2', '3']:
                cells = cell_arrays(data, intf, cap, comp, col)
                rng = np.random.default_rng(stable_seed('rel', comp, col, intf, cap))
                rs = np.array([r_cell(L, C, count) for L, C in cells])
                p, method = sign_flip(rs, rng)
                lo, hi = bootstrap(cells, count, rng, a.reps)
                row = dict(comparator=comp, endpoint=col, relayCap=cap, interference=intf,
                           n_cells=len(cells), effect_pct_geomean=round((np.exp(rs.mean()) - 1) * 100, 3),
                           ci95_low=round(lo, 3), ci95_high=round(hi, 3), p_raw=p, test=method)
                if not count:
                    row['effect_pct_meanpct'] = round(np.mean([(L.mean() / C.mean() - 1) * 100 for L, C in cells]), 3)
                fam.append(row)
        adj = holm(np.array([r['p_raw'] for r in fam]))
        for r, pa in zip(fam, adj):
            r['p_holm'] = round(float(pa), 5); r['significant_holm_0p05'] = int(pa < 0.05)
        rows += fam

fields = sorted({k for r in rows for k in r}, key=lambda k: list(rows[0]).index(k) if k in rows[0] else 99)
with open(a.out, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=fields); w.writeheader(); w.writerows(rows)
print(f'wrote {a.out} ({len(rows)} rows)')
