# Zenodo Deposit Guide

Two separate Zenodo records are recommended. Fill in the bracketed fields.

---

## Deposit 1 — Software (automatic, via GitHub integration)

**Do not upload manually.** Link the repository first, then create a release:

1. Sign in at https://zenodo.org with your GitHub account.
2. Go to https://zenodo.org/account/settings/github and switch the repository **On**.
3. On GitHub: **Releases → Create a new release**, tag `v1.0.0`, title
   "Initial archived release accompanying the paper", publish.
4. Zenodo mints a DOI within a few minutes. Two DOIs appear:
   - a **version DOI** (points at v1.0.0 specifically) — **cite this one in the paper**;
   - a **concept DOI** (always resolves to the newest version) — useful in the README.

`CITATION.cff` in the repository root supplies the author list, title, keywords
and license automatically, so the Zenodo metadata should need little editing.
Verify the author list and ORCIDs before publishing the release.

**Upload type:** Software
**License:** MIT

---

## Deposit 2 — Dataset (manual upload)

**Title**
```
Simulation results dataset for an ns-3 reproduction study of the LEO wireless sensor network routing metric
```

**Upload type:** Dataset
**License:** CC-BY-4.0 (standard for research data; permits reuse with attribution)

**Files to include**
```
results/raw/           all 7 CSV files
results/derived/       all screening and significance-test CSV files
data/fig2-digitized/   OK.csv, N_reception.csv, CRC.csv, README.md
figures/               all 4 PNG files
DATA_DICTIONARY.md     (essential — do not omit)
```

**Description** (paste as-is, adjusting the bracketed DOI once Deposit 1 exists)

```
This dataset contains the complete simulation output, derived statistical
analyses, and figures for an independent ns-3 reproduction study of the Low
Energy and Overhead (LEO) routing metric for wireless sensor networks
(Fitoš et al., Internet of Things 29:101472, 2025, doi:10.1016/j.iot.2024.101472).

CONTENTS

Raw simulation output (results/raw/): seven CSV files, each containing 2,800
independent simulation runs. The experimental grid is six topology layouts x
five environmental-attenuation values x four routing metrics x twenty
repetitions, plus a triangle-with-interference variant, executed separately for
three node-mobility conditions (static, bounded random walk, random waypoint)
and for three values of a per-flood re-relay bound. Each row records total
network energy, energy decomposed across seven frame types, and three ping
outcome counters.

Derived analyses (results/derived/): route-discrimination screening results,
identifying scenario cells in which the compared metrics select identical routes
and therefore carry no comparative information, together with Welch two-sample
significance tests restricted to the remaining cells.

Digitized source measurement (data/fig2-digitized/): a WebPlotDigitizer
extraction of the packet-reception-rate versus signal-to-noise-ratio curve
published as Figure 2 of the source paper, used to drive the simulator's
delivery model. This is a digitization of a published figure, not the original
authors' measurement data, which remains unpublished.

DATA DICTIONARY

DATA_DICTIONARY.md documents every file and every column, including units and
the reading conventions for the significance-test verdict fields. The dataset
can be reanalysed with the included Python scripts without installing ns-3.

NOTABLE FINDINGS RECORDED IN THIS DATA

Two results in this dataset materially affect interpretation of the source
metric. First, the device-specific link-quality cost formulation given in the
source publication collapses to a constant unit cost per link at realistic node
spacing, making it numerically indistinguishable from hop-count; this is visible
directly in the data, where the lqi-literal and hopcount rows are identical
across every condition. Second, the direction of the LEO-versus-hop-count energy
comparison inverts across the range of a per-flood re-relay bound that the source
publication does not specify, moving from 1.6% below hop-count at a bound of one
to 3.3% above it at a bound of three.

RELATED SOFTWARE

The ns-3 reconstruction that produced this data is archived separately at
[DOI OF DEPOSIT 1] and developed at [GITHUB URL].
```

**Keywords**
```
wireless sensor networks; routing metrics; ns-3; network simulation;
energy-efficient routing; reproducibility; research data; node mobility
```

**Related identifiers** (Zenodo's "Related/alternate identifiers" field)

| Relation | Identifier |
|---|---|
| `is supplement to` | DOI of your published paper, once assigned |
| `is compiled by` | DOI of Deposit 1 (the software) |
| `is derived from` | `10.1016/j.iot.2024.101472` (the source publication being reproduced) |

Setting `is derived from` correctly is worth doing: it makes the provenance
chain machine-readable and links your reproduction to the original work in
Zenodo's and OpenAIRE's graphs.

---

## After both deposits exist

Update two places:

**1. The paper**, Data and Code Availability section — replace the placeholder:

> The reconstruction is archived at https://doi.org/10.5281/zenodo.XXXXXXX
> (software, v1.0.0) and the derived simulation datasets at
> https://doi.org/10.5281/zenodo.YYYYYYY (dataset). Active development
> continues at https://github.com/USER/REPO. The repository README documents
> every modeling assumption and every deviation from the source publication,
> and DATA_DICTIONARY.md documents the structure of every released data file.

**2. The repository README** — add the Zenodo DOI badge at the top:

```markdown
[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.XXXXXXX.svg)](https://doi.org/10.5281/zenodo.XXXXXXX)
```

---

## A note on ordering

Create the software deposit first, so its DOI exists when you write the dataset
description. Both can be created before the paper is accepted; Zenodo DOIs are
permanent and can be cited in a manuscript under review.
