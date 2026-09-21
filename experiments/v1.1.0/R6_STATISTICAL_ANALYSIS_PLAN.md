# R6 — Statistical Analysis Plan

## Program
LEO v1.1.0 — Repair & Requalification

## Status
FROZEN BEFORE EFFECT CALCULATION

## Scope
Primary inference uses only `primary_static_source_constrained`.

The following are never pooled into the primary estimand:
- relayCap strata;
- interference strata;
- mobility;
- energy-accounting mode;
- Eq.17 policy.

Sensitivity and mobility-extension results are reported separately.

## Primary comparators
- LEO vs hopcount
- LEO vs lqi

`lqi-literal` is diagnostic, not a primary comparator.
## Primary endpoints
1. `totalEnergyMWs`
2. `pingTimeouts`
3. `pingNoRoute`
4. successful-ping route-fingerprint divergence

## Pairing
Main outcomes are paired by:
`layout, envFactor, interference, relayCap, run`.

Route evidence is paired by:
`layout, envFactor, interference, relayCap, run, targetId, seq`.

No outcome-dependent inclusion filtering is allowed.

## Scientific scenario unit
A scenario cell is:
`layout × envFactor × interference × relayCap`.

Within each cell, 20 paired runs estimate the cell effect.

Stratum summaries are equal-cell weighted so a scenario with more packets does not receive more scientific weight merely because it generated more route rows.
## Primary effect definition
For numeric endpoint Y and comparator C:

`D = Y_LEO - Y_C`.

For each scenario cell:
- compute the paired-run mean difference;
- compute the comparator and LEO cell means.

For each fixed `relayCap × interference` stratum:
- primary absolute effect = mean of cell mean differences;
- relative effect (%) = 100 × absolute effect / mean comparator cell mean.

Negative energy/count effects favor lower values under LEO.

## Confidence intervals
Use a deterministic hierarchical nonparametric bootstrap:
- resample scenario cells with replacement;
- within each selected cell, resample the 20 paired runs with replacement;
- 20,000 bootstrap replicates;
- fixed RNG seed 20260920;
- percentile 95% confidence interval.
## Primary hypothesis tests
For each numeric endpoint and primary comparator, test each of six pre-defined strata:
- relayCap 1, interference 0
- relayCap 2, interference 0
- relayCap 3, interference 0
- relayCap 1, interference 1
- relayCap 2, interference 1
- relayCap 3, interference 1

Test statistic:
equal-cell mean paired difference.

Null randomization:
cell-level sign flip of the cell mean paired differences.

- exact enumeration when number of cells <= 20;
- otherwise deterministic Monte Carlo with 200,000 sign patterns;
- RNG seed derived reproducibly from 20260920 plus endpoint/comparator/stratum identity;
- two-sided p-value.
## Multiplicity
For each `endpoint × comparator` family, apply Holm-Bonferroni correction across its six stratum p-values.

Final inferential flags use:
`Holm-adjusted p < 0.05`.

Raw p<0.05 alone is not a final claim.

## Route-fingerprint estimand
For each paired PING transaction:
- if both metrics succeed, compare `routeFingerprint`;
- route divergence = fingerprints differ.

For each run, compute divergence among jointly successful PINGs.
Then summarize by equal-cell weighting within each fixed `relayCap × interference` stratum.

Report:
- jointly successful transaction count;
- mean divergence fraction;
- hierarchical-bootstrap 95% CI.

No superiority p-value is assigned to route divergence.
## Sensitivity analyses
Eq.17 bounded sensitivity:
compare bounded LEO with matching primary literal-LEO configurations.

nRF52840 energy sensitivity:
compare matching nRF and primary configurations separately for hopcount and LEO; routing/count/route identity consistency is checked in addition to energy-accounting change.

OFAT sensitivity:
compare each non-reference reconstruction parameter setting against the frozen reference.

Mobility extension:
analyze walk and waypoint separately; exploratory only; never merged into primary inference.

## Output discipline
All R6 outputs must be generated from R5 PASS data only.
No cell is removed because LEO and comparator happen to be equal.
No route identity is inferred from total-energy equality.
No R6 result changes R4/R5 frozen inputs.
