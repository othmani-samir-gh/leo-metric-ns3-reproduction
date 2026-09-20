# R7 — Figures / Claims / Release-Candidate Requalification

## Program
LEO v1.1.0 — Repair & Requalification

## Gate
R7 — Figures / Claims / Release-Candidate Requalification

## Status
PASS — R7 CLOSED

---

# 1. Baseline

R6 authoritative closeout commit:

d3c03b2d47fa6bdba4c6de54970ae6895daadb01

R7 content commit:

a2d1b4d552c1c66a63126a32cacba73c16cf2e88

R7 content tree:

96915bdd9147c44f1f6a93a59cce0410503bcb96

Historical v1.0.0 artifacts remain preserved as legacy provenance only.

---

# 2. R7 objective

R7 rebuilt the public-facing figures, captions, claims, and repository documentation from the R6 statistical outputs rather than from the historical v1.0.0 analysis path.

The gate explicitly removed or labeled:
- stale v1.0.0 significance claims;
- outcome-dependent discrimination filtering;
- energy-equality route-identity inference;
- incorrect 19,600-row release counts;
- outdated claims that mobility data were absent;
- ambiguous Eq.12–13 implementation wording;
- legacy figure/statistical scripts as current analysis tools.

---

# 3. New authoritative figure pipeline

Authoritative generator:

analysis/r7_make_figures.py

SHA256:

604427091a5a3aa87cbbdaa492919dd6016d1d665cef406cae3803dc87a21668

The generator is dependency-light and consumes checksum-bound R6 outputs.

Generated figures:

1. fig1_energy_vs_hopcount.svg
2. fig2_energy_vs_lqi.svg
3. fig3_availability_vs_hopcount.svg
4. fig4_route_divergence.svg
5. fig5_nrf_energy_ratio.svg
6. fig6_ofat_energy_sensitivity.svg
7. fig7_mobility_energy_exploratory.svg
8. fig8_interference_energy_limited.svg

All 8 SVG files are XML-valid.

---

# 4. Deterministic rebuild

The 8 SVG files and FIGURE_PROVENANCE.json were copied aside, the generator was rerun, and outputs were compared byte-for-byte.

Result:

R7_DETERMINISTIC_REBUILD = PASS
SVG_COUNT = 8
DETERMINISTIC_MISMATCHES = 0

Thus the release figures are reproducible from the frozen R6 artifacts.

---

# 5. Figure provenance

FIGURE_PROVENANCE.json contains 8/8 figure entries.

Each entry binds:
- exact figure SHA256;
- source R6 CSV;
- exact source SHA256.

Examples:

fig1_energy_vs_hopcount.svg
SHA256:
863b83ee4bbfe97f48f4bc8324e265b2dd14a5205576b0fe3efce24792c2266b

fig4_route_divergence.svg
SHA256:
e647322f244a8bc677937ca92664713b864674fb1c4121ab77af46063c91e6c9

fig8_interference_energy_limited.svg
SHA256:
749cbab65b4828d12f847696673b69827d3b17b054c0a72fd049b79ae289627f

The provenance file SHA256 is:

a8623a4a2b5765c53f4b8400e53eacee5a0e5799c37474a30f4e53a27dcc001b

---

# 6. Captions

Authoritative captions:

figures/v1.1.0/CAPTIONS.md

SHA256:

e5b0a0ec0fee048d7cac7b3c22aaee716c92595ee3927bc7a0aa07a9053153f2

Coverage:

8 / 8 figures

Captions explicitly distinguish:
- confirmatory primary results;
- direct routeFingerprint evidence;
- limited-evidence interference strata;
- descriptive OFAT sensitivity;
- exploratory mobility extension;
- nRF energy-scale sensitivity.

---

# 7. Claims matrix

Authoritative claim audit:

figures/v1.1.0/CLAIMS_MATRIX.csv

SHA256:

6cc665e5fbb1b53a83f723354e9441054a6d481577991ef70b1e3339c466875a

Claims:

15 / 15 classified

Status distribution:

SUPPORTED_CONFIRMATORY = 3
SUPPORTED_DESCRIPTIVE = 1
NOT_SUPPORTED = 4
CONTRADICTED = 2
CONTRADICTED_DEGENERATE = 1
LIMITED_UNDERPOWERED = 1
NOT_OBSERVED_IN_TESTED_DOMAIN = 1
EXPLORATORY_ONLY = 1
NOT_CLAIMED = 1

Key adjudications:
- LEO energy reduction vs hopcount is confirmatory only at relayCap=1 without interference.
- LEO timeout and no-route reductions vs hopcount are supported across non-interference relay caps.
- A generic LEO energy advantage vs operational LQI is contradicted.
- The literal LQI baseline is degenerate with hopcount in the static primary domain.
- Interference strata are underpowered for confirmatory significance.
- Mobility remains exploratory.
- Exact replay of unpublished source simulator code is not claimed.
- A universal “LEO is best” claim is not supported.

---

# 8. README and data-dictionary correction

README.md and DATA_DICTIONARY.md were updated to align with v1.1.0.

Final stale-claim scan result:

PASS

The current README now explicitly states that Eq.12–13 one-way-link rejection is LIVE in the v1.1.0 repair branch while v1.0.0 computed link usability but did not enforce rejection.

The old 19,600-row narrative and related stale release descriptions are no longer presented as current v1.1.0 facts.

---

# 9. Legacy scripts

The following historical scripts are retained for provenance but explicitly marked LEGACY v1.0.0 / not authoritative for v1.1.0 claims:

make_figs.py
significance_test.py
analyze-results.py
discrimination_check.py

The authoritative v1.1.0 paths are:

analysis/r6_statistical_reanalysis.py
analysis/r7_make_figures.py

---

# 10. R6 binding

R7 independently reverified the R6 analysis chain.

R6 checksum verification:

R6_CHECKSUM_RC = 0
R6_CHECKSUM_OK = 14
R6_CHECKSUM_BAD = 0

R6 QC:

status = PASS

R6_CHECKSUMS.sha256 SHA256:

615d09f92b20de55286d6995b10e62e1a3d4eccfd512f1b8cef1a92f94a5f5c1

R6_QC.json SHA256:

b62b240586ed69a71f7eae14847e0c3b1d9f43cc746dfdab4ea97a6db6f4f1ad

---

# 11. Final R7 QC

Authoritative final QC:

figures/v1.1.0/R7_FINAL_QC.json

SHA256:

4b8f5101fd3b139eb3c4ae86ded2edf5530554438830a08ba499857744ac3c62

Result:

status = PASS
svg_count = 8
svg_xml_valid = 8
figure_provenance_entries = 8
claims_count = 15
caption_coverage = 8
deterministic_figure_rebuild_byte_identical = true
README/Data Dictionary stale claim scan = PASS
Eq.12–13 current-vs-historical qualification = PASS
legacy scripts marked superseded = PASS
R6 QC status = PASS
failures = []

---

# 12. Expanded R7 checksum

Authoritative checksum manifest:

figures/v1.1.0/R7_CHECKSUMS.sha256

SHA256:

5b6b1f741895b1dfd2e09c8fba017e1c07ac3f3491e7a8b67221efce55a1eb4f

It binds 23 release/analysis artifacts including:
- README.md;
- DATA_DICTIONARY.md;
- four legacy-analysis scripts;
- R6 statistical analyzer;
- R7 figure generator;
- R6 QC/checksum chain;
- claims matrix;
- captions;
- figure provenance;
- R7 QC artifacts;
- all 8 SVG figures.

Verification:

R7_CHECKSUM_VERIFY_RC = 0
R7_CHECKSUM_ENTRIES = 23
R7_CHECKSUM_OK = 23
R7_CHECKSUM_FAILED = 0

---

# 13. Scientific release boundary

The v1.1.0 release-candidate narrative supported by R7 is deliberately narrower than the historical v1.0.0 narrative.

Supported:
- LEO changes routes materially relative to hopcount and operational LQI.
- Relative to hopcount, LEO reduces timeout/no-route outcomes across non-interference relay caps in the primary static reconstruction.
- LEO has a confirmatory energy reduction vs hopcount only at relayCap=1 in that primary non-interference design.
- Reconstruction assumptions materially affect energy results.
- nRF energy accounting changes absolute energy scale but not routing in the tested sensitivity set.

Not supported:
- universal LEO energy superiority;
- generic superiority over operational LQI;
- confirmatory interference claims;
- exact replay of unpublished source simulator implementation;
- treating mobility as part of the primary source-reproduction estimand.

---

# 14. Gate decision

R7 = PASS
R7 = CLOSED
R8 = OPEN

R7 exit criteria are satisfied:
- all release figures rebuilt from R6;
- deterministic byte-identical rebuild verified;
- provenance for every figure verified;
- claims independently classified;
- captions cover every figure;
- stale repository claims corrected;
- legacy scripts clearly deprecated for v1.1.0;
- R6 chain independently reverified;
- expanded 23-entry checksum verifies 23/23;
- R7 final QC has zero failures.

---

# 15. Next allowed action

Open:

R8 — Final Independent Requalification

R8 must attack the complete v1.1.0 chain from R0 through R7, rerun critical regressions, revalidate the release hashes, perform a final claim/data consistency audit, and determine whether the repository is eligible for a v1.1.0 release candidate.

ASTRA should be used only if a real callable ASTRA binding becomes available. No substitute model may be labeled ASTRA.
