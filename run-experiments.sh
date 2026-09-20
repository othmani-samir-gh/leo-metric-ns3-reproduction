#!/usr/bin/env bash
# run-experiments.sh
#
# Batch driver reproducing the Section 4 sweep: for each layout and each
# environmental-factor value, run all three metrics, RUNS repetitions each,
# appending to a single CSV. Adjust NNODES/SPACING/ENV_FACTORS per layout to
# match the specific scenario you are targeting (Fig. 4/5 use different
# node counts and environmental factors per layout/scenario name, e.g.
# "b(2.5)", "f(2.25)", "fI(2.5)"; this driver gives you the mechanics, you
# choose the exact factor list to sweep and must document it in the paper).
set -euo pipefail

OUT_CSV="leo-results.csv"
RUNS=20
SEED_BASE=1
NNODES=20
SPACING=15.0

LAYOUTS=("around" "one-side" "u-shaped" "line" "circle" "triangle")
ENV_FACTORS=(2.0 2.25 2.5 2.625 3.0)
METRICS=("hopcount" "lqi" "leo")
# Add "lqi-literal" to compare against Eq. (2)-(3) applied exactly as
# published (no correction) -- see route-metrics.h's ZigbeeLqiLiteralMetric
# doc comment for the verified directional-inconsistency finding this
# reveals. Left out of the default sweep since it roughly doubles the LQI
# portion of the run time; add it explicitly when you need that comparison:
#   METRICS=("hopcount" "lqi" "lqi-literal" "leo")

rm -f "${OUT_CSV}"

for layout in "${LAYOUTS[@]}"; do
  for env in "${ENV_FACTORS[@]}"; do
    for metric in "${METRICS[@]}"; do
      echo ">> layout=${layout} env=${env} metric=${metric}"
      ./ns3 run "leo-topologies \
        --layout=${layout} \
        --nNodes=${NNODES} \
        --spacingM=${SPACING} \
        --envFactor=${env} \
        --metric=${metric} \
        --runs=${RUNS} \
        --seedBase=${SEED_BASE} \
        --outCsv=${OUT_CSV}"

      # Triangle-with-interference variant (Section 4's "fI" scenarios),
      # now actually implemented (see README's P0-3 fix) instead of being
      # a documented no-op.
      if [[ "${layout}" == "triangle" ]]; then
        echo ">> layout=${layout} env=${env} metric=${metric} interference=true"
        ./ns3 run "leo-topologies \
          --layout=${layout} \
          --nNodes=${NNODES} \
          --spacingM=${SPACING} \
          --envFactor=${env} \
          --metric=${metric} \
          --runs=${RUNS} \
          --seedBase=${SEED_BASE} \
          --interference=true \
          --outCsv=${OUT_CSV}"
      fi
    done
  done
done

echo "Done. Results in ${OUT_CSV}"
