#!/usr/bin/env python3
"""
nrf_comparative.py — recompute the LEO-versus-hop-count energy effect under
the datasheet-anchored nRF52840 energy model (paper Section D).

Route identity alone does not show that comparative energy effects survive a
change of energy-accounting model, because a different model can weight
transmission, reception and control traffic differently even when every
route is the same. This computes the comparison directly.

The nrf52840_energy_sensitivity set contains both LEO and hop-count for the
same cells as the primary set, so for each stratum it reports the effect
under the datasheet model alongside the effect under the simplified proxy on
the identical cells. Estimand as in the primary analysis: per cell, mean over
runs of (LEO - hop-count) matched by run index; per stratum, equal-cell mean
difference over the hop-count equal-cell mean. Exact two-sided cell-level
sign-flip test.

Usage, from the repository root:
    python3 analysis/nrf_comparative.py --out results/v1.1.1/nrf_comparative.csv
"""
import argparse, csv, glob, statistics
from collections import defaultdict
from itertools import product

NRF = 'results/v1.1.0/configs/nrf52840_energy_sensitivity'
PRI = 'results/v1.1.0/configs/primary_static_source_constrained'

def load(path):
    return {int(r['run']): r for r in csv.DictReader(open(path))}

def stats(pairs):
    diffs, bases = [], []
    for L, C in pairs:
        runs = sorted(set(L) & set(C))
        diffs.append(statistics.mean(float(L[r]['totalEnergyMWs']) - float(C[r]['totalEnergyMWs']) for r in runs))
        bases.append(statistics.mean(float(C[r]['totalEnergyMWs']) for r in runs))
    n = len(diffs); obs = abs(sum(diffs) / n)
    p = sum(1 for s in product((1, -1), repeat=n)
            if abs(sum(a * x for a, x in zip(s, diffs)) / n) >= obs - 1e-12) / 2 ** n
    return 100 * statistics.mean(diffs) / statistics.mean(bases), p, n

ap = argparse.ArgumentParser()
ap.add_argument('--out', default='results/v1.1.1/nrf_comparative.csv')
a = ap.parse_args()

cells = defaultdict(dict)
for d in glob.glob(f'{NRF}/*'):
    name = d.rsplit('/', 1)[-1].replace('__nrf', '')
    lay, e, i, c, m = name.split('__')
    cells[(lay, e[1:], i[1:], c[3:])][m] = f'{d}/main.csv'

def primary(lay, env, intf, cap, metric):
    return f'{PRI}/{lay}__e{env}__i{intf}__cap{cap}__{metric}/main.csv'

rows = []
print(f"{'stratum':20s}{'proxy':>10}{'datasheet':>12}{'shift':>9}{'p (datasheet)':>15}  cells")
for intf in ['0', '1']:
    for cap in ['1', '2', '3']:
        keys = sorted(k for k in cells if k[2] == intf and k[3] == cap and {'leo', 'hopcount'} <= set(cells[k]))
        nrf = [(load(cells[k]['leo']), load(cells[k]['hopcount'])) for k in keys]
        pri = [(load(primary(*k, 'leo')), load(primary(*k, 'hopcount'))) for k in keys]
        pe, pp, n = stats(pri)
        ne, np_, _ = stats(nrf)
        rows.append(dict(interference=intf, relayCap=cap, n_cells=n,
                         effect_proxy_pct=round(pe, 3), p_proxy=round(pp, 5),
                         effect_datasheet_pct=round(ne, 3), p_datasheet=round(np_, 5),
                         shift_pp=round(ne - pe, 3)))
        print(f'cap{cap} interference={intf}  {pe:+9.2f}%{ne:+11.2f}%{ne - pe:+8.2f}{np_:15.4f}  {n}')

with open(a.out, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print(f'\nwrote {a.out}')
