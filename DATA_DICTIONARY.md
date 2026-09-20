# Data Dictionary

> **Version scope.** Sections describing `results/raw/`,
> `results/derived/`, the PNG figures, `discrimination_check.py`, or
> `significance_test.py` document the **legacy v1.0.0 artefacts only**.
> They are retained for provenance and must not be used for v1.1.0 scientific
> claims. The authoritative v1.1.0 dataset is `results/v1.1.0/`, its
> statistical analysis is `analysis/r6_statistical_reanalysis.py`, and its
> release-candidate figures are `figures/v1.1.0/`.

All energy values are in **mWs (milliwatt-seconds)** and represent the sum over
all nodes for a single simulation run. All counts are per run.

---

## 1. Raw experiment output — `results/raw/`

Produced directly by `scratch/leo-topologies.cc` (via `run-experiments.sh`).
One row = one independent simulation run.

| File | Condition | Re-relay cap | Seed | Rows |
|---|---|---|---|---|
| `mob_none.csv` | Static nodes | 3 | 42 | 2800 |
| `mob_walk.csv` | Bounded random walk | 3 | 42 | 2800 |
| `mob_waypoint.csv` | Random waypoint (60 s warm-up) | 3 | 42 | 2800 |
| `seed42_cap1.csv` | Static nodes | 1 | 42 | 2800 |
| `seed42_cap2.csv` | Static nodes | 2 | 42 | 2800 |
| `seed42_cap3.csv` | Static nodes | 3 | 42 | 2800 |
| `leo-results-cap1.csv` | Static nodes | 1 | 1 | 2100 |

`mob_none.csv` and `seed42_cap3.csv` are byte-identical: the static condition at
cap 3 serves both as the mobility baseline and as the cap-3 point of the
sensitivity sweep. Both names are retained so each analysis is self-contained.

### Columns

| Column | Type | Meaning |
|---|---|---|
| `layout` | string | Node topology: `around`, `one-side`, `u-shaped`, `line`, `circle`, `triangle`. Reproduces the six layouts of Fig. 4 of the source paper in shape; exact coordinates are unpublished and were reconstructed (see README). |
| `envFactor` | float | Environmental attenuation exponent *N* in the path-loss equation. Values swept: 2.0, 2.25, 2.5, 2.625, 3.0. Higher = faster signal decay = sparser effective connectivity. |
| `metric` | string | Routing metric under test: `leo`, `hopcount`, `lqi` (generic ZigBee delivery-probability cost), `lqi-literal` (nRF52840 formulation applied verbatim). |
| `interference` | 0/1 | 1 = triangle-with-interference variant (extra SNR penalty on all links incident on a hypotenuse node). Only non-zero for `layout=triangle`. |
| `run` | int | Repetition index, 0–19. The ns-3 RNG run number is `run+1`. |
| `totalEnergyMWs` | float | Total network energy for the run, summed over all nodes and all frame types. |
| `pingTimeouts` | int | Pings that were transmitted but received no reply within the timeout. **Reliability measure.** |
| `pingNoRoute` | int | Ping attempts abandoned before transmission because no route existed. Kept separate from `pingTimeouts`; add both for a total-failure count. |
| `pingSent` | int | Pings actually transmitted. `pingSent + pingNoRoute` = attempts scheduled. |
| `energy_netScan` | float | Energy attributed to NET_SCAN frames (connection phase). |
| `energy_connect` | float | Energy attributed to CONNECT frames (connection phase). |
| `energy_pathDiscovery` | float | Energy attributed to PATH_DISCOVERY flood frames. **The component that dominates LEO's overhead.** |
| `energy_pathDiscoveryReply` | float | Energy attributed to PATH_DISCOVERY_REPLY unicasts. |
| `energy_ping` | float | Energy attributed to PING data frames. **The component the LEO metric is designed to minimise.** |
| `energy_pingReply` | float | Energy attributed to PING_REPLY frames. |
| `energy_ack` | float | Energy attributed to link-layer ACK frames. |

The seven `energy_*` columns sum to `totalEnergyMWs`.

---

## 2. Route-discrimination screening — `results/derived/d*.csv`

Produced by `discrimination_check.py`. One row = one scenario cell
(layout × envFactor × interference).

Historical purpose: screen cells using equality of total energy as a proxy
for route identity. The v1.1.0 audit found this to be methodologically invalid:
energy equality is not direct route evidence, and filtering cells after
observing the outcome changes the estimand. These files are historical only.
v1.1.0 uses direct per-PING `routeFingerprint` evidence and performs primary
analysis on the full pre-specified cell population without this filter.

| File | Corresponding raw data |
|---|---|
| `d_none.csv` | `mob_none.csv` |
| `d_walk.csv` | `mob_walk.csv` |
| `d_wp.csv` | `mob_waypoint.csv` |
| `d42_c1.csv`, `d42_c2.csv`, `d42_c3.csv` | `seed42_cap1/2/3.csv` |
| `disc_cap1.csv` | `leo-results-cap1.csv` |

### Columns

| Column | Type | Meaning |
|---|---|---|
| `layout`, `envFactor`, `interference` | — | Cell identifier, as above. |
| `identicalRuns` | int | Runs in which LEO and hop-count energy matched to within 1e-9. |
| `totalRuns` | int | Runs compared in this cell (normally 20). |
| `identicalPct` | float | `100 × identicalRuns / totalRuns`. |
| `verdict` | string | `DISCRIMINATING` if `identicalPct < 50`, else `degenerate`. Only `DISCRIMINATING` cells enter the statistical analysis. |

---

## 3. Legacy significance tests — `results/derived/s*.csv`, `sig_*.csv`

These v1.0.0 files were produced by `significance_test.py` after the
outcome-dependent screening described above. They are retained only for
audit/provenance and are **superseded**.

Historical method: Welch's independent two-sample t-test, two-tailed,
α = 0.05. The v1.1.0 R6 workflow instead uses paired scenario/run identity,
cell-level sign-flip inference, hierarchical-bootstrap confidence intervals,
direct route evidence, and Holm correction.

| File | Data source | Dependent variable |
|---|---|---|
| `s_none_e.csv` / `s_none_r.csv` | static | total energy / ping timeouts |
| `s_walk_e.csv` / `s_walk_r.csv` | bounded walk | total energy / ping timeouts |
| `s_waypoint_e.csv` / `s_waypoint_r.csv` | random waypoint | total energy / ping timeouts |
| `sig_cap1_energy.csv`, `sig_cap1_rel.csv`, `sig_cap1_ping.csv` | cap 1, seed 1 | total energy / timeouts / data energy |

### Columns

| Column | Type | Meaning |
|---|---|---|
| `layout`, `envFactor` | — | Scenario cell. |
| `comparedMetric` | string | The metric LEO is being tested against. |
| `n_leo`, `n_compared` | int | Sample sizes entering the test. |
| `t_statistic` | float | Welch's t. Negative = LEO mean lower. |
| `p_value` | float | Two-tailed p. |
| `pctDiff_leo_vs_compared` | float | `100 × (mean_LEO − mean_other) / mean_other`. Negative = LEO lower (better, for energy and timeouts). |
| `verdict_on_<column>` | string | `significant` / `not_significant`, plus direction (`leo_lower` or `leo_higher`). |

**Reading note.** `verdict` uses the phrasing "favour LEO" to mean *lower value*,
which is the desirable direction for both energy and timeouts. The column name
records which dependent variable was tested, since the same script is used for
energy, reliability, and per-frame-type breakdowns.

---

## 4. Digitized source-paper measurement — `data/fig2-digitized/`

WebPlotDigitizer extraction of Fig. 2 of the source publication (PRR and PER
versus SNR). Format: `SNR_dB, percent`, no header.

| File | Curve |
|---|---|
| `OK.csv` | Successful reception (562 points). **This is the curve the simulator uses.** |
| `N_reception.csv` | No reception (533 points). Not consumed by the code. |
| `CRC.csv` | CRC error (577 points). Not consumed by the code. |

Self-consistency check: the three curves sum to ~100% at matching SNR values
(mean deviation 0.19 percentage points across 0–79 dB), as they must for three
mutually exclusive per-frame outcomes.

**This is a digitization of a published figure, not the original authors'
measurement data**, which remains unpublished. Pixel-level reading error is
inherent. See `data/fig2-digitized/README.md` for full provenance.

---

## 5. Figures

### Legacy v1.0.0 — `figures/*.png`

The four PNG files generated by `make_figs.py` use the superseded
discrimination-screening workflow and are historical only.

### v1.1.0 release candidate — `figures/v1.1.0/`

Regenerate with:

```bash
python3 analysis/r7_make_figures.py
```

The generator is dependency-free Python and reads checksum-bound R6 outputs.
It creates eight SVG figures plus `FIGURE_PROVENANCE.json`,
`CLAIMS_MATRIX.csv`, and `CAPTIONS.md`.

---

## Reproducing the v1.1.0 analysis from frozen files

No ns-3 installation is required once the governed R5 dataset exists.

```bash
python3 analysis/r6_statistical_reanalysis.py
python3 analysis/r7_make_figures.py
```

The R6 analyzer requires NumPy but not SciPy or pandas. The R7 SVG generator
uses only the Python standard library.

To regenerate the raw R5 data itself, ns-3 is required; see README.md and
`experiments/v1.1.0/frozen_plan.jsonl`.
