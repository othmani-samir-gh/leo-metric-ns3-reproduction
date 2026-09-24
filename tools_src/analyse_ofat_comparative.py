#!/usr/bin/env python3
"""
analyse_ofat_comparative.py — turn the OFAT sensitivity set into a
*comparative* result.

For every tested parameter value it computes the LEO-minus-baseline effect
using the same estimand as the primary analysis (run-paired differences within
the cell, expressed as a percentage of the baseline mean), and compares it with
the effect at the reference configuration. The question answered is:

    does this unpublished parameter change the comparison between metrics,
    or does it merely move every metric's absolute energy together?

Because a single scenario cell provides no second level to resample, the
uncertainty here is a paired bootstrap over the 20 repetitions, and the test is
a paired permutation over repetitions. This is weaker evidence than the primary
analysis and must be reported as such.

Usage:
    python3 tools/analyse_ofat_comparative.py                 \
        --leo    results/v1.1.0/configs/reconstruction_ofat_sensitivity \
        --others results/v1.1.1/ofat_comparative                        \
        --out    results/v1.1.1/ofat_comparative_effects.csv
"""
import argparse, csv, glob, os, random, statistics

def load(path):
    out = {}
    for r in csv.DictReader(open(path)):
        out[int(r['run'])] = r
    return out

def paired(leo, base, col):
    runs = sorted(set(leo) & set(base))
    d = [float(leo[i][col]) - float(base[i][col]) for i in runs]
    b = [float(base[i][col]) for i in runs]
    return d, b

def perm_p(d, n=200000, seed=11):
    obs = abs(statistics.mean(d)); rng = random.Random(seed); c = 0
    for _ in range(n):
        if abs(statistics.mean([v if rng.random() < .5 else -v for v in d])) >= obs - 1e-12: c += 1
    return (c + 1) / (n + 1)

def boot_ci(d, b, n=20000, seed=13):
    rng = random.Random(seed); k = len(d); es = []
    for _ in range(n):
        idx = [rng.randrange(k) for _ in range(k)]
        es.append(100 * statistics.mean([d[i] for i in idx]) / statistics.mean([b[i] for i in idx]))
    es.sort(); return es[int(.025 * n)], es[int(.975 * n)]

ap = argparse.ArgumentParser()
ap.add_argument('--leo', required=True, help='v1.1.0 LEO-only OFAT directory')
ap.add_argument('--others', required=True, help='v1.1.1 comparative directory')
ap.add_argument('--col', default='totalEnergyMWs')
ap.add_argument('--out', default='ofat_comparative_effects.csv')
a = ap.parse_args()

# LEO runs: reference plus each variant (scenarioId = param__value)
leo = {os.path.basename(d): load(f'{d}/main.csv') for d in glob.glob(f'{a.leo}/*')}
ref_leo = leo.get('reference') or leo[[k for k in leo if 'reference' in k][0]]

rows = []
for d in sorted(glob.glob(f'{a.others}/*')):
    sid = os.path.basename(d)
    if '__' not in sid: continue
    *rest, metric = sid.split('__')
    key = '__'.join(rest)
    base = load(f'{d}/main.csv')
    src = ref_leo if key == 'reference' else leo.get(key)
    if src is None:
        print(f'  skip {sid}: no matching LEO run'); continue
    dd, bb = paired(src, base, a.col)
    eff = 100 * statistics.mean(dd) / statistics.mean(bb)
    lo, hi = boot_ci(dd, bb)
    rows.append(dict(parameter=key, comparator=metric, effect_pct=round(eff, 3),
                     ci95_low=round(lo, 3), ci95_high=round(hi, 3),
                     p_permutation=round(perm_p(dd), 6), n_runs=len(dd)))

with open(a.out, 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)

print(f'wrote {a.out} ({len(rows)} comparative effects)\n')
ref = {r['comparator']: r['effect_pct'] for r in rows if r['parameter'] == 'reference'}
print(f"{'parameter':22s}{'comparator':13s}{'effect%':>9}{'ref%':>9}{'shift':>9}")
for r in sorted(rows, key=lambda r: (r['comparator'], r['parameter'])):
    if r['parameter'] == 'reference': continue
    shift = r['effect_pct'] - ref.get(r['comparator'], float('nan'))
    print(f"{r['parameter']:22s}{r['comparator']:13s}{r['effect_pct']:+9.2f}{ref.get(r['comparator'], 0):+9.2f}{shift:+9.2f}")
print('\n"shift" is the change in the LEO-vs-baseline comparison caused by the parameter.')
print('A parameter that only moves the operating point leaves the shift near zero.')
