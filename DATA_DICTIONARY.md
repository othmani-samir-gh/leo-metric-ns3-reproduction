# Data Dictionary

Describes every file under `results/` and every column in it, so the datasets
can be reanalysed without reading the simulation source or rerunning ns-3.

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
| `leo-results-cap1.csv` | Static nodes | 1 | 1 | 2800 |

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

Purpose: identify cells in which LEO and hop-count select *identical* routes.
In those cells the metrics produce bit-identical energy, so any statistical
comparison measures nothing about metric quality and must be excluded. This is
the screening step applied before every significance test reported in the paper.

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

## 3. Significance tests — `results/derived/s*.csv`, `sig_*.csv`

Produced by `significance_test.py`, restricted to discriminating cells.
One row = one (scenario cell × compared metric) pair, tested against LEO.

Test: Welch's two-sample t-test (unequal variance, two-tailed), α = 0.05.

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

## 5. Figures — `figures/`

Regenerate with `python3 make_figs.py` (requires the `results/` files in place).

| File | Content |
|---|---|
| `fig1_energy_breakdown.png` | Energy by frame type, per metric, per mobility condition. |
| `fig2_cap_sensitivity.png` | LEO-vs-hop-count energy difference across re-relay cap 1–3. |
| `fig3_per_layout.png` | Per-layout total energy, static condition. |
| `fig4_discrimination.png` | Share of discriminating scenario cells per mobility condition. |

---

## Reproducing the analysis from these files alone

No ns-3 installation is required to reanalyse the results:

```bash
python3 discrimination_check.py results/raw/mob_none.csv --out d_none.csv
python3 significance_test.py results/raw/mob_none.csv \
        --only-discriminating d_none.csv --column totalEnergyMWs
python3 make_figs.py
```

To regenerate the raw data itself, ns-3 is required; see README.md.
