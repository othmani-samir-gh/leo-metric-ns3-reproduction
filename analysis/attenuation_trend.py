#!/usr/bin/env python3
"""
attenuation_trend.py — reproduce the attenuation-trend analysis (paper
Section G, Figure 7).

The source study claims LEO's advantage grows as node availability worsens.
This re-analyses the frozen primary data at each attenuation factor N
separately, with the same run-paired estimand as the primary analysis:
per cell, mean over runs of (LEO - baseline), matched by run index; per
stratum, equal-cell mean difference divided by the baseline's equal-cell
mean. Each point pools the six layouts at one N, so the exact two-sided
cell-level sign-flip test has 2^6 = 64 sign patterns and a minimum
attainable p-value of 2/64 = 0.031.

Reads only results/v1.1.0/configs/primary_static_source_constrained/.

Usage, from the repository root:
    python3 analysis/attenuation_trend.py --out results/v1.1.1/attenuation_trend.csv
"""
import argparse, csv, glob, statistics
from collections import defaultdict
from itertools import product

PRI = 'results/v1.1.0/configs/primary_static_source_constrained'
LAYOUTS = ['around', 'one-side', 'u-shaped', 'line', 'circle', 'triangle']
ENVS = ['2', '2.25', '2.5', '2.625', '3']

def load():
    data = defaultdict(dict)
    for d in glob.glob(f'{PRI}/*'):
        for r in csv.DictReader(open(f'{d}/main.csv')):
            data[(r['layout'], r['envFactor'], r['interference'], r['relayCap'], r['metric'])][int(r['run'])] = r
    return data

def effect(data, env, cap, comp, col):
    diffs, bases = [], []
    for lay in LAYOUTS:
        L = data[(lay, env, '0', cap, 'leo')]; C = data[(lay, env, '0', cap, comp)]
        runs = sorted(set(L) & set(C))
        diffs.append(statistics.mean(float(L[i][col]) - float(C[i][col]) for i in runs))
        bases.append(statistics.mean(float(C[i][col]) for i in runs))
    n = len(diffs); obs = abs(sum(diffs) / n)
    p = sum(1 for s in product((1, -1), repeat=n)
            if abs(sum(a * b for a, b in zip(s, diffs)) / n) >= obs - 1e-12) / 2 ** n
    return 100 * statistics.mean(diffs) / statistics.mean(bases), p

def slope(xs, ys):
    mx, my = statistics.mean(xs), statistics.mean(ys)
    return sum((a - mx) * (b - my) for a, b in zip(xs, ys)) / sum((a - mx) ** 2 for a in xs)

ap = argparse.ArgumentParser()
ap.add_argument('--out', default='results/v1.1.1/attenuation_trend.csv')
a = ap.parse_args()

data = load()
rows = []
for comp in ['hopcount', 'lqi']:
    for col in ['totalEnergyMWs', 'pingTimeouts']:
        for cap in ['1', '2', '3']:
            effs = []
            for env in ENVS:
                e, p = effect(data, env, cap, comp, col)
                effs.append(e)
                rows.append(dict(comparator=comp, endpoint=col, relayCap=cap, envFactor=env,
                                 n_cells=len(LAYOUTS), effect_pct=round(e, 3), p_exact=round(p, 5)))
            s = slope([float(v) for v in ENVS], effs)
            print(f'{comp:9s} {col:15s} cap{cap}  ' +
                  '  '.join(f'N={v}:{x:+6.1f}%' for v, x in zip(ENVS, effs)) +
                  f'   trend {s:+.1f} pp/unit N')

with open(a.out, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print(f'\nwrote {a.out} ({len(rows)} rows)')
