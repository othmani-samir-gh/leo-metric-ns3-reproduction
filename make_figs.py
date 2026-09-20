#!/usr/bin/env python3
"""Generate publication tables (as data) and figures from the experiment CSVs."""
import csv, statistics, json
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

def load(p): return list(csv.DictReader(open(p)))
def disc(p):
    return {(r['layout'], r['envFactor'], r['interference'])
            for r in csv.DictReader(open(p)) if r['verdict'] == 'DISCRIMINATING'}

def per_metric(rows, col, dsc):
    d = defaultdict(list)
    for r in rows:
        k = (r['layout'], r['envFactor'], r.get('interference', '0'))
        if k in dsc:
            d[r['metric']].append(float(r[col]))
    return {m: (statistics.mean(v), statistics.stdev(v) if len(v) > 1 else 0.0, len(v))
            for m, v in d.items()}

METRICS = ['leo', 'hopcount', 'lqi', 'lqi-literal']
LBL = {'leo': 'LEO', 'hopcount': 'Hop-count', 'lqi': 'LQI (Eq.1)', 'lqi-literal': 'LQI-literal (Eq.2-3)'}
CONDS = [('mob_none.csv', 'd_none.csv', 'Static'),
         ('mob_walk.csv', 'd_walk.csv', 'Bounded walk'),
         ('mob_waypoint.csv', 'd_wp.csv', 'Random waypoint')]

out = {}

# ============ TABLE 1: main results per mobility condition ============
t1 = []
for f, d, name in CONDS:
    rows = load(f); dsc = disc(d)
    rec = {'condition': name, 'cells': len(dsc)}
    for col, key in [('totalEnergyMWs', 'total'), ('energy_pathDiscovery', 'pd'),
                     ('energy_ping', 'ping'), ('pingTimeouts', 'to')]:
        pm = per_metric(rows, col, dsc)
        rec[key] = {m: pm[m] for m in METRICS if m in pm}
    t1.append(rec)
out['table1'] = t1

# ============ TABLE 2: cap sensitivity ============
t2 = []
for cap in [1, 2, 3]:
    rows = load(f'seed42_cap{cap}.csv'); dsc = disc(f'd42_c{cap}.csv')
    rec = {'cap': cap, 'cells': len(dsc)}
    for col, key in [('totalEnergyMWs', 'total'), ('energy_pathDiscovery', 'pd'),
                     ('pingTimeouts', 'to')]:
        pm = per_metric(rows, col, dsc)
        rec[key] = {m: pm[m] for m in METRICS if m in pm}
    t2.append(rec)
out['table2'] = t2

# ============ TABLE 3: per-layout, static ============
rows = load('mob_none.csv'); dsc = disc('d_none.csv')
bylay = defaultdict(lambda: defaultdict(list))
for r in rows:
    k = (r['layout'], r['envFactor'], r.get('interference', '0'))
    if k in dsc:
        bylay[r['layout']][r['metric']].append(float(r['totalEnergyMWs']))
t3 = []
for lay in sorted(bylay):
    m = {k: statistics.mean(v) for k, v in bylay[lay].items()}
    if 'leo' in m and 'hopcount' in m:
        t3.append({'layout': lay, 'leo': m['leo'], 'hopcount': m['hopcount'],
                   'lqi': m.get('lqi'), 'pct': 100*(m['leo']-m['hopcount'])/m['hopcount']})
out['table3'] = t3

# ============ TABLE 4: discrimination rates ============
t4 = []
for f, d, name in CONDS:
    all_cells = list(csv.DictReader(open(d)))
    nd = sum(1 for r in all_cells if r['verdict'] == 'DISCRIMINATING')
    t4.append({'condition': name, 'disc': nd, 'total': len(all_cells),
               'pct': 100*nd/len(all_cells)})
out['table4'] = t4

with open('tables.json', 'w') as fh:
    json.dump(out, fh, indent=1, default=str)

# =================== FIGURES ===================
plt.rcParams.update({'font.size': 9, 'font.family': 'DejaVu Sans',
                     'axes.grid': True, 'grid.alpha': 0.3, 'grid.linestyle': ':'})
GREY = ['#2b2b2b', '#6e6e6e', '#a8a8a8', '#d0d0d0']

# --- Fig 1: energy breakdown by mobility condition ---
fig, axes = plt.subplots(1, 3, figsize=(9.5, 3.1), sharey=True)
for ax, rec in zip(axes, t1):
    xs = np.arange(len(METRICS)); w = 0.6
    pd_ = [rec['pd'][m][0] for m in METRICS]
    pg = [rec['ping'][m][0] for m in METRICS]
    tot = [rec['total'][m][0] for m in METRICS]
    other = [t - p - g for t, p, g in zip(tot, pd_, pg)]
    ax.bar(xs, pd_, w, label='Path discovery', color=GREY[0])
    ax.bar(xs, pg, w, bottom=pd_, label='Data (ping)', color=GREY[1])
    ax.bar(xs, other, w, bottom=[a+b for a, b in zip(pd_, pg)], label='Other', color=GREY[2])
    ax.set_xticks(xs); ax.set_xticklabels([LBL[m] for m in METRICS], rotation=30, ha='right', fontsize=7.5)
    ax.set_title(f"{rec['condition']} (n={rec['cells']} cells)", fontsize=9)
axes[0].set_ylabel('Mean network energy (mWs)')
axes[0].legend(fontsize=7.5, loc='upper left', framealpha=0.9)
plt.tight_layout()
plt.savefig('fig1_energy_breakdown.png', dpi=200, bbox_inches='tight')
plt.close()

# --- Fig 2: cap sensitivity ---
fig, ax1 = plt.subplots(figsize=(5.4, 3.4))
caps = [r['cap'] for r in t2]
e = [100*(r['total']['leo'][0]-r['total']['hopcount'][0])/r['total']['hopcount'][0] for r in t2]
p = [100*(r['pd']['leo'][0]-r['pd']['hopcount'][0])/r['pd']['hopcount'][0] for r in t2]
ax1.axhline(0, color='k', lw=0.8)
ax1.plot(caps, p, 'o-', color=GREY[0], label='Path-discovery energy')
ax1.plot(caps, e, 's--', color=GREY[1], label='Total network energy')
for xx, yy in zip(caps, e):
    ax1.annotate(f'{yy:+.1f}%', (xx, yy), textcoords='offset points', xytext=(0, -14), ha='center', fontsize=7.5)
for xx, yy in zip(caps, p):
    ax1.annotate(f'{yy:+.1f}%', (xx, yy), textcoords='offset points', xytext=(0, 7), ha='center', fontsize=7.5)
ax1.set_xlabel('Per-flood re-relay cap (unspecified in source paper)')
ax1.set_ylabel('LEO vs Hop-count (%)\nnegative = LEO lower')
ax1.set_xticks(caps); ax1.legend(fontsize=8); ax1.set_ylim(-6, 22)
plt.tight_layout(); plt.savefig('fig2_cap_sensitivity.png', dpi=200, bbox_inches='tight'); plt.close()

# --- Fig 3: per-layout static energy ---
fig, ax = plt.subplots(figsize=(6.2, 3.2))
lays = [r['layout'] for r in t3]; xs = np.arange(len(lays)); w = 0.26
ax.bar(xs-w, [r['leo'] for r in t3], w, label='LEO', color=GREY[0])
ax.bar(xs, [r['hopcount'] for r in t3], w, label='Hop-count', color=GREY[1])
ax.bar(xs+w, [r['lqi'] for r in t3], w, label='LQI (Eq.1)', color=GREY[2])
ax.set_xticks(xs); ax.set_xticklabels(lays, rotation=15)
ax.set_ylabel('Mean total energy (mWs)'); ax.legend(fontsize=8)
ax.set_title('Static condition, discriminating cells only', fontsize=9)
plt.tight_layout(); plt.savefig('fig3_per_layout.png', dpi=200, bbox_inches='tight'); plt.close()

# --- Fig 4: discrimination rate ---
fig, ax = plt.subplots(figsize=(4.6, 3.0))
names = [r['condition'] for r in t4]; pcts = [r['pct'] for r in t4]
bars = ax.bar(names, pcts, color=[GREY[0], GREY[1], GREY[2]], width=0.55)
for b, r in zip(bars, t4):
    ax.annotate(f"{r['disc']}/{r['total']}", (b.get_x()+b.get_width()/2, b.get_height()),
                textcoords='offset points', xytext=(0, 3), ha='center', fontsize=8)
ax.set_ylabel('Discriminating scenario cells (%)'); ax.set_ylim(0, 110)
plt.tight_layout(); plt.savefig('fig4_discrimination.png', dpi=200, bbox_inches='tight'); plt.close()

print("figures + tables.json written")
for r in t4: print(r)
