# R5 — Raw Data Regeneration

## Program
**LEO v1.1.0 — Repair & Requalification**

## Status
`PASS — R5 CLOSED`

## Authoritative execution identity
```text
source commit = c95c9548bd790170697d3d35a401c618290c04dd
source tree   = a0f60011722f87f61710fe96ad54f1adf2047b8f
ns-3 commit   = d2add90b452d600cfb4859baed8e9ea633519447
binary SHA256 = 7422bd9951a645553edabe7335c62270a076516c130523f006ec96e42b1de5eb
manifest SHA256 = f36d0510e3dbb0d3c9adffa565c74ad2c5026b77fa1cf04b411ba46f49f4c520
plan SHA256 = fb6d360f6f1f88ea50e59b91d2ae147e0c22c2c3e9ff195212e8fe89c932428e
```

Historical v1.0.0 data were not overwritten.

## Frozen-plan execution
```text
primary_static_source_constrained  420 / 420
eq17_bounded_sensitivity            36 / 36
nrf52840_energy_sensitivity         72 / 72
reconstruction_ofat_sensitivity     17 / 17
mobility_extension                  72 / 72
TOTAL                              617 / 617
```

## Global integrity
```text
simulation/main rows = 12,340
route-evidence rows  = 2,344,600
unique main keys     = 12,340
unique route keys    = 2,344,600

success  = 1,576,339
timeout  =   229,367
no_route =   538,894
pending  =         0

RECEIPT_STATUS      = {"PASS": 617}
FAILURES            = 0
R5_GLOBAL_INTEGRITY = PASS
```

Route checks:
- no duplicate global route keys;
- successful route fingerprints are non-zero;
- successful hop counts are in [1,64];
- pending route transactions are zero.

Main-row checks:
- finite/non-negative energy values;
- `pingSent + pingNoRoute = 190`;
- `pingTimeouts <= pingSent`;
- row and manifest identities match the frozen plan.

## Energy serialization adjudication
An absolute component-reconciliation threshold of 1e-3 mWs flagged 561 rows in the nRF52840 sensitivity.

This was a CSV precision effect, not an energy-accounting failure:
```text
max absolute residual = 0.005519999999933134 mWs
max relative residual = 5.40962796595985e-06
relative tolerance     = 1e-5
rows failing tolerance = 0
```

## Recovery adjudication
One configuration originally had a minimal `RECOVERED_EXISTING_COMPLETE` receipt:
```text
primary_static_source_constrained
u-shaped__e3__i0__cap3__lqi
```

The original directory was archived and the exact frozen configuration was rerun through the official R5 runner.

The replacement receipt is `PASS`. Regenerated outputs were byte-identical to the archived originals:
```text
main SHA256   = d11956c6ecdf4c9c8724a5978d24502b7e51dbe84f6f3b975a704025965ddb82
routes SHA256 = 4e862b327101dc6ef235be187791656d66ca564804a906022180dc1618399476
```

## Closeout artifacts
```text
results/v1.1.0/R5_INTEGRITY.json
SHA256 = b018228161ab2d30be37076258f3ef641fb6f161f7d23910643986a384f043f0

results/v1.1.0/PROVENANCE.json
SHA256 = ed84f061a646192c4cf045c43a8777f13a9a5933ca56fce64dd85b4e01caf179

results/v1.1.0/CHECKSUMS.sha256
SHA256 = 6c584cf361be3d3b9763931817fedabd65a076634e948fe59d119b3757cb3545
```

Checksum verification:
```text
entries = 2,478
RC      = 0
OK      = 2,478
failed  = 0
```

## Scientific boundary
R5 establishes completeness, integrity, and provenance. It does not establish metric superiority, statistical significance, or exact replay of unpublished source simulator code.

ASTRA was not run because no callable ASTRA binding/provider is available in the active environment. No substitute reviewer was labeled as ASTRA.

## Gate decision
```text
R5 = PASS
R5 = CLOSED
R6 = OPEN
```

## Next allowed action
Open:
```text
R6 — Statistical Reanalysis
```

R6 must:
- use the source-constrained static set for primary analysis;
- keep relay caps and interference strata explicit;
- pair on frozen scenario/run identity;
- use direct route fingerprints;
- avoid outcome-dependent discrimination filtering;
- report paired effect estimates and confidence intervals;
- define multiplicity correction before final claims;
- report sensitivity and mobility-extension analyses separately.
