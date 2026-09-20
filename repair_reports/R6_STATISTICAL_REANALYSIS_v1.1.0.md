# R6 — Statistical Reanalysis

## Status
`PASS — R6 CLOSED`

## Frozen statistical method
SAP commit:
`d90a925cddc4318a9857bf7c42182fa989df886b`

SAP SHA256:
`dfe5ed8d6a84190d8ca34d5a2f8f26b59087cd964795b0099b30e0dce119d588`

Primary analysis:
- source-constrained static set only;
- LEO vs hopcount and LEO vs operational LQI;
- relayCap and interference kept separate;
- equal-cell weighted effects;
- 20,000-replicate hierarchical bootstrap CI;
- exact sign-flip for five-cell interference strata;
- 200,000 deterministic sign flips for 30-cell non-interference strata;
- Holm correction across six strata per endpoint/comparator family;
- no outcome-dependent filtering.
## Primary findings

### LEO vs hopcount, non-interference
Energy:
```text
cap1  -4.968%  Holm p=0.000210  significant
cap2  -0.226%  Holm p=0.907930  not significant
cap3  +1.896%  Holm p=0.542877  not significant
```

Ping timeouts are lower at all caps after Holm:
```text
cap1 -18.484%
cap2 -21.115%
cap3 -21.658%
```

Ping no-route counts are lower at all caps after Holm:
```text
cap1  -5.872%
cap2 -12.371%
cap3 -12.276%
```

Therefore the energy effect is relay-cap dependent; a universal LEO energy-saving claim is not supported.
### LEO vs LQI, non-interference
Energy is higher under LEO:
```text
cap1  +6.077%   Holm p=0.043520
cap2 +11.370%   Holm p=0.000700
cap3 +13.739%   Holm p=0.000210
```

Ping-timeout differences are not Holm-significant.

PingNoRoute is significantly higher at cap1 (+16.381%, Holm p=0.001440); cap2/3 are not Holm-significant.

### Interference strata
Each interference stratum has only five scenario cells. Exact two-sided sign-flip testing has 32 patterns and minimum attainable p=0.0625.

No interference stratum is Holm-significant. These results remain limited-evidence sensitivity results.
## Direct route evidence
Equal-cell mean route divergence among jointly successful PINGs:

```text
LEO vs hopcount:
cap1 I0 14.65%
cap2 I0 17.13%
cap3 I0 16.90%

LEO vs LQI:
cap1 I0 27.14%
cap2 I0 28.32%
cap3 I0 28.42%
```

Route identity is based on direct `routeFingerprint`, never energy equality.

## LQI-literal diagnostic
Across all 105 static primary configuration pairs:
```text
main rows compared = 2,100; mismatches = 0
route rows compared = 399,000
status mismatches = 0
fingerprint mismatches = 0
hop-count mismatches = 0
```

`LQI_LITERAL_HOPCOUNT_DEGENERACY = CONFIRMED`.
## Sensitivities
Eq.17 bounded vs literal:
```text
main endpoint effects = 0
route transactions compared = 136,800
status/fingerprint/hop mismatches = 0
```
The policies are non-discriminating over the tested frozen sensitivity domain.

nRF52840 energy accounting:
```text
route transactions compared = 273,600
status/fingerprint/hop mismatches = 0
count mismatch runs = 0
energy scale ≈ 12.0×–12.3× simplified proxy
```

OFAT shows material reconstruction sensitivity, including ack timeout, background noise, discovery timeout, interference penalty, spacing, and radio overhead.
## Mobility extension
Exploratory only.

Walk:
```text
LEO vs hopcount energy +3.32%, timeouts -17.13%, no-route -9.10%
LEO vs LQI      energy +19.00%, no-route +16.33%
route divergence ≈30.9% vs hopcount; ≈44.4% vs LQI
```

Waypoint:
```text
LEO vs hopcount energy +9.54%, timeouts -10.89%, no-route -4.71%
LEO vs LQI      energy +23.13%, timeouts +10.52%, no-route +15.54%
route divergence ≈35.6% vs hopcount; ≈50.6% vs LQI
```
## R6 QC
```text
R6_ANALYSIS = PASS
deterministic rerun = PASS
byte-identical outputs = 13
primary summary rows = 36
primary cell-effect rows = 630
primary route rows = 12
Holm-significant primary strata = 11
QC failures = 0
R6_QC = PASS
```

Hashes:
```text
analysis script =
400379c42aa17e673a7f038b9d5400ca0d63c41c5f7ea5bc060c6f26a051dc0d

R6_QC =
b62b240586ed69a71f7eae14847e0c3b1d9f43cc746dfdab4ea97a6db6f4f1ad

R6_CHECKSUMS =
615d09f92b20de55286d6995b10e62e1a3d4eccfd512f1b8cef1a92f94a5f5c1
```

14/14 R6 checksum entries verify.
## Scientific conclusion
The repaired analysis supports a narrower interpretation:
- LEO materially changes routing;
- versus hopcount, timeout/no-route reductions are more robust than energy savings;
- energy benefit versus hopcount is relay-cap dependent;
- versus operational LQI, LEO consumes more energy in all non-interference caps;
- interference inference is underpowered at five cells;
- reconstruction assumptions materially affect results;
- literal LQI is degenerate with hopcount in the static primary set;
- nRF accounting changes energy scale without changing routing.

No universal `LEO is best` claim is supported.

## Gate decision
```text
R6 = PASS
R6 = CLOSED
R7 = OPEN
```

Next: `R7 — Figures / Claims / Release-Candidate Requalification`.
