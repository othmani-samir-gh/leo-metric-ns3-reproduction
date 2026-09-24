#!/usr/bin/env python3
"""
make_ofat_comparative_plan.py — emit the 51 missing baseline configurations
for the comparative OFAT extension, in the exact schema of the v1.1.0 frozen
plan, so they can be executed with the repository's own runner.

The v1.1.0 one-factor-at-a-time set was run for LEO only, in a single scenario
cell. It shows how far each unpublished parameter moves LEO's own absolute
energy, but not whether it changes the comparison between metrics. This script
clones each of those 17 LEO rows for the three baseline metrics, changing only
--metric, --scenarioSet, --scenarioId and the output paths. Every other flag is
copied verbatim from the archived plan, so the parameter values cannot drift.

    17 LEO rows x 3 baselines = 51 configurations x 20 runs = 1,020 runs

The LEO runs themselves are NOT repeated: the archived results are reused, and
seeds are unchanged, so every comparison stays run-paired.

Usage, from the repository root:

    python3 tools/make_ofat_comparative_plan.py \
        --plan experiments/v1.1.0/frozen_plan.jsonl \
        --out  experiments/v1.1.1/ofat_comparative_plan.jsonl
"""
import argparse, json, pathlib, sys

SRC_SET = 'reconstruction_ofat_sensitivity'
NEW_SET = 'ofat_comparative'
BASELINES = ['hopcount', 'lqi', 'lqi-literal']

ap = argparse.ArgumentParser()
ap.add_argument('--plan', default='experiments/v1.1.0/frozen_plan.jsonl')
ap.add_argument('--out', default='experiments/v1.1.1/ofat_comparative_plan.jsonl')
a = ap.parse_args()

rows = [json.loads(l) for l in open(a.plan) if l.strip()]
src = [r for r in rows if r['scenarioSet'] == SRC_SET]
if not src:
    raise SystemExit(f'no rows found for scenarioSet={SRC_SET}')
for r in src:
    assert '--metric=leo' in r['argv'], f"{r['scenarioId']} is not a LEO row"
print(f'source rows: {len(src)} (expected 17)', file=sys.stderr)

raw = f'results/v1.1.1/raw/{NEW_SET}.csv'
route = f'results/v1.1.1/routes/{NEW_SET}_routes.csv'
out = []
for r in src:
    for metric in BASELINES:
        sid = f"{r['scenarioId']}__{metric}"
        argv = []
        for f in r['argv']:
            if f.startswith('--metric='):        argv.append(f'--metric={metric}')
            elif f.startswith('--scenarioSet='): argv.append(f'--scenarioSet={NEW_SET}')
            elif f.startswith('--scenarioId='):  argv.append(f'--scenarioId={sid}')
            elif f.startswith('--outCsv='):      argv.append(f'--outCsv={raw}')
            elif f.startswith('--routeCsv='):    argv.append(f'--routeCsv={route}')
            else:                                argv.append(f)
        out.append(dict(argv=argv, manifestSha256=r['manifestSha256'], rawCsv=raw,
                        routeCsv=route, runs=r['runs'], scenarioId=sid, scenarioSet=NEW_SET))

assert len(out) == len(src) * 3
p = pathlib.Path(a.out); p.parent.mkdir(parents=True, exist_ok=True)
with p.open('w') as f:
    for r in out:
        f.write(json.dumps(r, sort_keys=True) + '\n')

runs = sum(r['runs'] for r in out)
print(f'wrote {a.out}: {len(out)} configurations, {runs} runs', file=sys.stderr)
