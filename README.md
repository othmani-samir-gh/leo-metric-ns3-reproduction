# LEO metric — ns-3 reconstruction and reproduction study

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.22859623.svg)](https://doi.org/10.5281/zenodo.22859623)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)



An independent ns-3 reconstruction of the **LEO** (Low Energy and Overhead)
wireless-sensor-network routing metric, with hop-count and two ZigBee LQI
baselines, released as the artefact accompanying a reproduction study.

> **Source publication under reproduction:**
> M. Fitoš, J. Ďuďák, G. Gašpar, J. Machaj, P. Brída, "LEO: An innovative
> metric for energy-efficient routing in wireless sensor networks,"
> *Internet of Things*, vol. 29, 101472, 2025.
> https://doi.org/10.1016/j.iot.2024.101472
>
> This is an independent reimplementation. It is not authored by, endorsed
> by, or derived from the original authors' code, which is unpublished.

## What this repository contains

| Path | Contents |
|---|---|
| `contrib/leo-wsn/` | The ns-3 contrib module: metrics, ATPC, channel, routing application |
| `scratch/` | Scenario driver and the equation cross-check program |
| `results/raw/` | 19,600 simulation runs across 7 experiment configurations |
| `results/derived/` | Route-discrimination screening and significance tests |
| `data/fig2-digitized/` | Digitized PRR/SNR curve from the source paper's Fig. 2 |
| `figures/` | Publication figures, regenerable via `make_figs.py` |
| `DATA_DICTIONARY.md` | Every file, every column, every unit |

## Two findings that affect how this metric should be evaluated

**1. The device-specific LQI baseline degenerates.** The nRF52840 link cost
given as Eq. (2)–(3) of the source paper, `max(1, (RSSI+92)/32)`, is pinned at
its floor of 1.0 for any RSSI below −60 dBm. Under the source paper's own
path-loss constants that means any inter-node spacing beyond roughly 1.5 m, so
at realistic deployment distances the metric is numerically identical to
hop-count. This is visible directly in `results/raw/`, where the `lqi-literal`
and `hopcount` rows match in every condition. The same equations also invert
direction — a stronger received signal yields a *higher* cost — which conflicts
with the paper's own statement that lower LQI denotes better quality.

**2. The headline energy result depends on an unspecified parameter.** LEO
re-broadcasts a discovery frame when a better-metric copy arrives, with no
upper bound stated in the source paper. A bound is required in practice; its
value inverts the result, from 1.6% below hop-count at a bound of one to 3.3%
above it at a bound of three. See `figures/fig2_cap_sensitivity.png`.

## Quick start

```bash
# 1. Verify the equations independently of any simulation
python3 equation_oracle.py                      # 13/13 expected

# 2. Place the module in an ns-3 tree (3.40+, tested on 3.48)
cp -r contrib/leo-wsn  <ns-3-dir>/contrib/
cp scratch/*.cc        <ns-3-dir>/scratch/
cd <ns-3-dir> && ./ns3 configure --disable-examples --disable-tests && ./ns3 build

# 3. Cross-check the C++ against the Python oracle
./ns3 run leo-equation-check 2>/dev/null | grep '^eq' > cpp_out.txt
python3 equation_oracle.py --emit-values > py_out.txt
diff cpp_out.txt py_out.txt && echo "ORACLE AND C++ AGREE"

# 4. Run a scenario
./ns3 run "leo-topologies --layout=triangle --envFactor=2.5 --metric=leo --runs=20"
```

To reanalyse the released results **without installing ns-3**, see the last
section of `DATA_DICTIONARY.md`.

## Citing

See `CITATION.cff`. Please cite both this artefact and the original LEO
publication.

---

## Equation-to-code map

| Paper | Meaning | Code |
|---|---|---|
| Eq. (1) | ZigBee generic link cost from p_l | `ZigbeeLqiMetric::LinkCostFromDeliveryProbability` |
| Eq. (2) | nRF52840 link cost from LQI | `ZigbeeLqiMetric::LinkCostFromLqi` |
| Eq. (3) | LQI = RSSI + 92 | `ZigbeeLqiMetric::LqiFromRssiDbm` |
| Eq. (4) | R = PER/(1-PER) | not called directly — see note below |
| Eq. (5) | P_s = SNR_s + N | (used conceptually to set `backgroundNoiseDbm`) |
| Eq. (6) | LSL = RSSI_m − P_TX − G_RX | `ComputeLinkSignalLossDb` (atpc.h) |
| Eq. (7) | P_TX,adj = SNR_th − SNR | `ComputeTxPowerAdjustmentDb` |
| Eq. (8) | SNR_th = SNR_s + 5 dB | `ComputeSnrThresholdDb` |
| Eq. (9) | P_th = SNR_th + N + G_RX | `ComputeRequiredReceivePowerDbm` |
| Eq. (10)| P_TX,R = P_th − LSL | `ComputeRequiredTxPowerFromLsl` |
| Eq. (11)| P_p = Σ P_l_i (route = sum of link metrics) | `IRouteMetric::CombineRoute` |
| Eq. (12)-(13) | one-way link rejection | `LeoMetric::IsBidirectional` |
| Eq. (14)| P_l = R·P_TX,max[mW] + P_TX[mW] + P_R[mW] | `LeoMetric::LinkPowerMw` |
| Eq. (15)| dBm→mW conversion | `DbmToMw` / `MwToDbm` |
| Eq. (16)| R = max(avg(R_AB,R_BA), R_D) | `LeoMetric::AverageRetransmissions` |
| Eq. (17)| R_D from power deficiency; v1.1.0 exposes bounded-vs-literal policy explicitly (`boundEq17ToRMax`) | `LeoMetric::RetransmissionsFromPowerDeficiency` |
| Eq. (18)| link signal loss vs. distance | `LinkSignalLossDb` (wsn-channel.h) |
| Algorithm 1 | pre-TX ATPC computation | `AlgorithmOnePreTransmission` |
| Algorithm 2 | post-RX ReqTXP update | `AlgorithmTwoPostReception` |
| Fig. 2 | empirical PRR(SNR) curve | `kPrrCurve` / `PrrFromSnrDb` (wsn-channel.cc) |
| Fig. 4 | six node layouts | `BuildLayout` (leo-topologies.cc) |

`Eq. (4)` (R from PER) is not used as a standalone call because, in this
simulation, retransmission counts are **observed directly** from a
simplified stop-and-wait ARQ (`SendUnicastReliable`/ACK) rather than
derived analytically from a measured PER — see "Assumptions" below for why
this is a deliberate design choice, not an oversight.

## Module layout

```
contrib/leo-wsn/
  model/
    neighbor-table.{h,cc}   Neighbor Table (Section 3.2)
    atpc.{h,cc}              Algorithms 1 & 2, Eq. (6)-(10)
    route-metrics.{h,cc}     Hop-count / ZigBee LQI / LEO, Eq. (1)-(3),(11),(14)-(17)
    wsn-channel.{h,cc}       Path loss (Eq. 18) + empirical PRR curve (Fig. 2)
    wsn-net-device.{h,cc}    Minimal radio endpoint + link-info tag
    wsn-routing-app.{h,cc}   Flooding route discovery + ping campaign + stats
scratch/
  leo-topologies.cc          Driver reproducing Fig. 4 layouts / Section 4 methodology
run-experiments.sh           Batch sweep (layout x envFactor x metric x runs)
analyze-results.py           Mean/95% CI summary, Fig. 5-7 style
```

## Build

Tested design target: ns-3.40+ (CMake build system). Copy `contrib/leo-wsn`
into `<ns-3-dir>/contrib/`, copy `scratch/leo-topologies.cc` into
`<ns-3-dir>/scratch/`, then:

```bash
cd <ns-3-dir>
./ns3 configure --enable-examples
./ns3 build
./ns3 run "leo-topologies --layout=circle --nNodes=20 --envFactor=2.5 --metric=leo --runs=20"
```

If your ns-3 checkout predates the CMake build (pre-3.36, waf-based), use
the `wscript` template commented at the bottom of
`contrib/leo-wsn/CMakeLists.txt` instead.

If your ns-3 version does not auto-generate a per-module aggregated header
(`ns3/leo-wsn-module.h`), replace that `#include` in
`scratch/leo-topologies.cc` with the six individual headers listed right
below it in the same file (already provided, just commented out).

**This code has not been build-tested against a live ns-3 tree in this
environment** (no network access to the ns-3 distribution from this
sandbox). Before running production experiments, do a clean build and fix
any API-signature drift for your exact ns-3 version (Tag/Header
serialization signatures in particular have changed across ns-3 releases).
Treat this as a carefully-derived reference implementation, not a
guaranteed drop-in binary.

## Assumptions and limitations (disclose these in your methodology)

The paper is explicit about many constants (SNR_s = +24 dB, ATPC offset =
+5 dB, one-way-link offset = +10 dB, R_max = 4, P_R = 1 mW, signal loss
per meter = −64 dBm, environmental factor 2–3) — all of these are used
as-is. A few implementation details are **not** published numerically and
had to be chosen here; report them explicitly if you use this code:

1. **Fig. 2 PRR(SNR) curve -- RESOLVED (Sept 2026).** `kPrrCurve` in
   `wsn-channel.cc` is now digitized directly from the paper's Fig. 2 "OK"
   curve via WebPlotDigitizer (562 points in the current second-pass
   re-extraction, SNR ~1.1-77.8 dB, ~6x denser through the steep
   12-20 dB transition than the first pass -- see
   `data/fig2-digitized/README.md` for the before/after consistency
   comparison), not a
   qualitative-landmark approximation as in an earlier revision. See
   `data/fig2-digitized/README.md` for the raw exports (including the
   companion "No reception"/"CRC error" curves) and provenance caveats
   (digitization pixel-level error; internal self-consistency check
   against the two companion curves). Still not the authors' own raw
   measurement data -- prefer that over this digitization if you can
   obtain it.
2. **EMA smoothing constant** for R_RX/R_TX (`kDefaultEmaAlpha = 0.2` in
   `neighbor-table.h`) — the paper says "exponential moving average" but
   does not publish α. Treat as a sweep parameter.
3. **MAC retry cap and per-attempt timing** (`m_macMaxRetries`,
   `m_txSlotDurationS`, `m_rxSlotDurationS`, `m_ackTimeoutS` in
   `wsn-routing-app.h`) are not specified by the paper at the level of a
   MAC-layer ARQ; this implementation adds an explicit stop-and-wait
   ACK/retry loop so that genuine retransmission counts exist to drive
   Eq. (16)-(17). Calibrate the slot durations against the nRF52840's
   actual on-air time for your chosen bit rate/payload if you need
   absolute (not just relative, cross-metric) energy figures.
4. **Node coordinates for Fig. 4 layouts.** `BuildLayout()` reproduces the
   *shape* of each layout (ring, line, U, triangle, etc.) parameterized by
   a nominal spacing; the authors' exact coordinates are not published.
   Sweep `spacingM` and `envFactor` as sensitivity parameters rather than
   relying on a single configuration for headline numbers.
4b. **Noise-floor / link-budget calibration (`backgroundNoiseDbm`).** The
   paper gives `SNR_s = +24 dB` (Section 3.1) but not the receiver's noise
   floor `N` (Eq. 5) directly. The default here (`-116 dBm`) was chosen so
   that, combined with `signalLossPerMDbm = -64 dBm` and `envFactor`
   2-3, single-hop links at a "typical" node spacing land in a plausible
   SNR range (multi-hop needed beyond a few tens of meters) -- but this is
   a **derived, not measured**, value and directly controls whether your
   topology ends up single-hop, multi-hop, or fully disconnected. Always
   sanity-check that your chosen `spacingM`/`envFactor`/`backgroundNoiseDbm`
   combination produces a *connected* network with a realistic mix of
   1-hop and multi-hop links before running the full sweep (e.g. log SNR
   values for a few links, or check that `pingSent > 0` in the output CSV
   -- `pingSent == 0` for every run means the network is effectively
   disconnected under the current link budget). `--backgroundNoiseDbm` is
   exposed on the command line specifically for this calibration step.
5. **Antenna/LNA gains** `G_TX`, `G_RX` are treated as 0 dB throughout
   (the paper notes they are "not constant in all directions" and excludes
   them from `P_TX`/`P_R` for the same reason); if your target hardware
   has known gains, wire them into `atpc.h`'s helper functions.
6. **Triangle-with-interference variant** ("fI" scenarios in Fig. 5/6)
   is implemented as a **declared reconstruction**, not a source-identical
   PHY model. `--interferenceDb` applies a per-link SNR penalty (default
   15 dB) to links touching the selected hypotenuse nodes. The paper/source
   material available to this reproduction does not publish a numeric dB
   penalty that justifies 15 dB, so this value is a sensitivity parameter,
   not an authoritative constant. The effective post-penalty SNR is now
   preserved in the Neighbor Table and reused by the first-contact LQI
   fallback, so routing and channel PRR no longer evaluate different SNRs.
7. **Transaction/time-out bookkeeping** is identity-safe in v1.1.0:
   ping timeouts are keyed by `(targetId,seq)`, path-discovery completion
   is matched to the current `(targetId,floodId)`, and failed discoveries
   have an explicit liveness timeout. `--ackTimeoutS` and
   `--discoveryTimeoutS` are reconstruction parameters and must be frozen
   in the experiment manifest; neither is claimed as a source-published
   timing constant.
9. **Destination-side route selection window (`m_discoveryWindowS`,
   default 0.15 s).** Earlier revisions of this code let each destination
   reply to whichever PATH_DISCOVERY copy arrived *first*, for every
   metric. The destination now waits `m_discoveryWindowS` after the first
   candidate arrives, collects any other candidates that arrive via
   different paths within that window, and replies via whichever had the
   lowest accumulated metric, for all three metric types, matching
   Section 2's general requirement that "the route with the best weight
   must have the best performance." The window duration itself is not
   specified by the paper and must be treated as a tunable parameter.
10. **ZigBee-LQI degeneracy (important -- read before drawing
    conclusions from Hop-count vs. LQI comparisons).** Two independent
    effects were found, while validating this module, to make the
    ZigBee-LQI baseline numerically collapse into Hop-count:
    (a) *Eq. (2)'s floor.* `C{l} = max(1, LQI/32)` with `LQI = RSSI+92`
    (Eq. 3) pins the link cost at exactly 1.0 for any RSSI below -60 dBm.
    Under the paper's own path-loss constants (Section 4: -64 dBm/m,
    N = 2-3), RSSI only exceeds -60 dBm at inter-node spacings below
    roughly 1.5 m -- unrealistic for a WSN deployment. So Eq. (2), applied
    at realistic WSN spacing, is *identically* 1.0 per link, i.e.
    identical to Hop-count, for essentially any plausible topology. For
    this reason `ZigbeeLqiMetric::ComputeLinkMetric` uses the *generic*
    ZigBee-specification cost, Eq. (1) (`C{l}` from the delivery
    probability `p_l`), as the primary/default LQI baseline instead;
    Eq. (2)-(3) remain available as the static helpers
    `LqiFromRssiDbm`/`LinkCostFromLqi` if you specifically want to
    reproduce -- and report -- the floor effect.
    (b) *No ACK history at first discovery.* Even with Eq. (1), `p_l` is
    normally learned from observed ARQ (ACK) outcomes, but the very first
    (and, in low-timeout scenarios, often the *only*) route discovery for
    a node happens before any unicast ACK exchange has occurred on that
    link, leaving `p_l` at one flat default for every link and again
    collapsing route selection to Hop-count-equivalent behaviour. This is
    now addressed by estimating `p_l` from the SNR measured on the very
    packet being processed, via the same empirical PRR(SNR) curve the
    channel itself uses (Fig. 2) -- see the SNR-based branch in
    `ZigbeeLqiMetric::ComputeLinkMetric`. Once real ACK history
    accumulates (e.g. after any ping timeout triggers rediscovery), the
    EMA-tracked observed delivery ratio takes over as the primary
    estimate.
    If you still observe Hop-count and LQI producing identical or
    near-identical results after these fixes on a *non-uniform* layout
    (e.g. `triangle`, `one-side`), that is worth re-investigating rather
    than assuming it is expected -- but on the maximally symmetric
    `circle` layout (equal inter-node spacing all around), some residual
    similarity between the two is mathematically expected regardless
    of the cost function used, since geometrically-uniform links tend to
    have geometrically-uniform quality.
8. **This is a single-process, non-PHY simulation.** ns-3 has no native
   nRF52840/BLE-mesh PHY module, and the authors used their own
   MeshProtocolSimulator, not ns-3. `WsnChannel`/`WsnNetDevice` deliberately
   abstract the PHY/MAC into the same passive, SNR/PRR-driven model the
   paper itself uses for its metric (this is explicitly the point of LEO:
   its inputs are exactly RSSI/SNR/retransmission-count/TX-power, nothing
   more), rather than attempting to reuse an unrelated ns-3 PHY (e.g.
   802.11 or 802.15.4) whose channel model would not match the paper's
   measured Fig. 2 curve anyway.

## Post-review correctness fixes (external code audit)

An independent line-by-line audit against the paper (compared here to a
Notion-based review report) identified several functional-correctness bugs
beyond the modeling assumptions listed above. These are **bugs**, not
assumptions, and were fixed as follows:

1. **ACK key mismatch (critical).** `AckKey()` originally matched on
   `hdr.type`, but the ACK frame built in `OnReceive` sets `type =
   FrameType::ACK`, which never equals the original DATA frame's type
   (PING/PING_REPLY/PATH_DISCOVERY_REPLY). Every ACK therefore silently
   failed to match its pending transaction, forcing every unicast frame
   through its *entire* retry budget regardless of actual delivery
   success, and -- more importantly -- causing `OnAckTimeout` to record a
   **failure** sample via `UpdateDeliveryOutcome` on every single attempt,
   even when the frame was genuinely received. This directly corrupted
   the EMA-tracked delivery ratio that Section "ZigBee-LQI degeneracy"
   above relies on as the primary `p_l` estimate for Eq. (1), and
   inflated R_TX-driven energy accounting for every metric. Fixed by
   adding a `WsnHeader::origType` field that always carries the
   *underlying data transaction's* type (set automatically in
   `BroadcastFrame`/`SendUnicastReliable`, and explicitly copied from the
   DATA frame when building its ACK); `AckKey()`/`ForwardKey()` now match
   on `origType`, not `type`.
2. **Retransmission count read from the wrong place.** `OnReceive` fed
   `NeighborTable::UpdateRxRetransmissions` from `tag.retransmissionCount`
   (a `WsnLinkInfoTag` field that `WsnChannel::Send` never actually
   populates -- it stays at its default of 0 always) instead of
   `hdr.retransmissionCount` (the real, deserialized value carried on the
   packet itself). Fixed to read from `hdr`. The tag's now-dead
   `retransmissionCount`/`isRetransmission` fields are kept only for
   layout stability and documented as unused.
3. **PING not forwarded through intermediate nodes.** `HandlePing`
   checked `hdr.targetId != m_nodeId` and, on mismatch, simply dropped the
   frame instead of forwarding it toward the real destination. Any route
   requiring more than one hop from the gateway therefore failed *every*
   ping deterministically, as a pure simulation artifact rather than a
   physical-layer outcome -- likely inflating the timeout rates reported
   in every multi-hop scenario. Fixed by adding a forwarding branch
   mirroring `HandlePingReply`'s (with the same idempotency/loop guards).
4. **Energy unit error (x1000).** `ChargeEnergy` computed
   `powerMw * durationS * 1000.0`; since mW * s already equals mWs with no
   further conversion, this inflated every absolute energy figure by
   1000x. This was a *constant* factor (it did not change the relative
   ordering between Hop-count/LQI/LEO within a run) but invalidated any
   comparison to the paper's absolute Fig. 5 numbers. Fixed by removing
   the erroneous factor.
5. **Receive-side energy never charged.** `ChargeEnergy` was only ever
   invoked with `isTx=true`; the receive-cost branch (`P_R`, Eq. 14) it
   already supported was simply never exercised. Fixed by charging RX
   energy once per successfully decoded frame in `OnReceive`.

**Any leo-results.csv collected before this fix pass should be discarded
and the sweep re-run** -- items 1-3 affect the underlying delivery-ratio
and route-selection logic that this whole simulation exists to compare,
not just cosmetic output.

### RESOLVED: "unicast" now genuinely rejects unaddressed frames

`WsnChannel::Send` evaluates delivery independently against *every*
attached device based on that device's own link quality to the sender --
there is no concept of a physical point-to-point link. A "unicast" call
is really an omnidirectional transmission; without an explicit addressing
check, any other node within probabilistic range would silently process
(and be charged energy for) a frame addressed to someone else.

**This was flagged Critical in an earlier review round and has since been
fixed:** `WsnHeader` now carries an explicit `intendedNextHopId`, set by
`SendUnicastReliable` (and by the ACK-construction code in `OnReceive`)
to the exact node the frame is meant for at this hop. `OnReceive` rejects
-- with no further processing and, critically, **no energy charge** --
any non-broadcast frame whose `intendedNextHopId` does not match the
local node id, before doing anything else. Broadcast types
(NET_SCAN/CONNECT/PATH_DISCOVERY) are exempt by design, since they are
meant for every listener.

This was not just a theoretical concern: it was empirically confirmed to
significantly inflate `energy_ack` in well-connected scenarios (a small,
very-frequently-sent frame type like ACK gets "overheard" and RX-charged
by many bystander nodes under the broadcast channel model before this
fix). **Any `leo-results.csv` collected before this specific fix should
be discarded**, in addition to the discard notice above for the
AckKey/retransmissionCount/Ping-forwarding fixes -- this is a distinct,
later bug from the same root cause (no addressing check at all before
this fix; the AckKey fix only addressed *transaction matching*, not
*reception eligibility*).

### FURTHER REFINEMENT: point-to-point delivery at the channel, not just filtering at the receiver

The `intendedNextHopId` fix above corrected the *outcome* (bystanders no
longer processed/were charged energy for frames not addressed to them),
but `WsnChannel::Send` still computed distance/LSL/SNR/PRR and rolled a
delivery decision for *every* attached device on every unicast
transmission, only to have all-but-one silently rejected at the receiver
-- wasted computation (O(N) per unicast send instead of O(1)), and a
subtler issue: RNG draws per unicast transmission scaled with network
size N, which is not necessary and complicates run-to-run
reproducibility reasoning as N changes.

Following a second external review round, `WsnChannel` now exposes a
`SendUnicast(sender, packet, txPowerDbm, intendedNextHopId)` method that
evaluates the distance -> LSL -> SNR -> PRR -> interference pipeline for
the single intended link only, rolling exactly one delivery decision --
while `WsnChannel::Send` (unchanged) continues to broadcast to every
attached device for NET_SCAN/CONNECT/PATH_DISCOVERY, which are genuinely
meant for every listener. `WsnNetDevice::SendTo(packet,
intendedNextHopId)` wraps this for callers; `WsnRoutingApp::
SendUnicastReliable` and the ACK-construction code in `OnReceive` now
call `SendTo` instead of the broadcast `Send`. This changes nothing about
the underlying physical model (the exact same per-link SNR/PRR/
interference computation runs for the link that matters) or about
`kPrrCurve`/`LeoMetric`/ATPC -- it is purely an efficiency and RNG-hygiene
refinement, not a re-architecture of the channel model into a
conventional `PointToPointChannel`. The `intendedNextHopId` check in
`OnReceive` is kept as a defense-in-depth safety net even though it
should now be unreachable for genuine unicast traffic (see its updated
comment).

## Second-round fixes (fix_plan.md batches P0-P2)

A follow-up, more granular fix plan (`fix_plan.md`, batches P0/P1/P2) was
implemented in full except where noted:

- **P0-1 (ping-outcome column semantics).** Added `NodeStats::pingNoRoute`,
  separate from `pingTimeouts` (which now means "sent but no reply").
  CSV gained a `pingNoRoute` column.
- **P0-2 (`WsnChannel::GetDevice` signature) -- NOT applied, and here is
  why.** `fix_plan.md` asserts `ns3::Channel::GetDevice` takes `uint32_t`
  and that our `std::size_t` override therefore leaves `WsnChannel`
  abstract and uncompilable. This is contradicted by direct evidence: the
  exact code with the `std::size_t` signature was built and run
  successfully multiple times on a real ns-3.48 checkout during this
  project's development (producing valid CSV output across dozens of
  scenarios). If your ns-3 version is older and genuinely uses `uint32_t`
  here, you will get a real "does not override" compiler error --
  in that case, and only then, change the override to `uint32_t`.
- **P0-3 (interference variant).** `WsnChannel::SetLinkSnrPenaltyDb`
  implemented; `leo-topologies.cc` now applies `--interferenceDb` (default
  15 dB, undocumented in the paper -- a declared stand-in, not a published
  value) to every link touching a hypotenuse node when
  `--layout=triangle --interference=true`. `analyze-results.py` includes
  an integrity check that flags interference=0/1 pairs whose mean energy
  is suspiciously close.
- **P0-4 (dead-code honesty).** See the equation-status table below;
  `CombineRoute` and `MwToDbm` remain genuinely unused (dead) helpers.
  `AlgorithmOnePreTransmission`/`AlgorithmTwoPostReception`/
  `LeoMetric::IsBidirectional` are now LIVE (see P1-1).
- **P1-1 (Eq. 12-13 one-way-link rejection, previously dead).**
  `WsnHeader` gained `pTxAdjDb`/`pThDbm`. The WITH_FEEDBACK adjustment
  (Eq. 7) is now computed at the receiver, carried back in the ACK, and
  *applied by the original sender* in `HandleAck` via
  `AlgorithmTwoPostReception` -- matching Fig. 3(a) exactly, instead of
  the previous version's unilateral self-update at the receiver (which
  never populated `pTxRequestedByNeighborKnown`, leaving Eq. 12-13 dead).
  The WITHOUT_FEEDBACK branch (Eq. 9-10, Fig. 3c) is likewise now wired
  through `pThDbm` and runs in `OnReceive` upon receiving the DATA frame.
- **P1-2 (layout geometry).** `u-shaped` now has two vertical arms
  connected by a bottom segment (a real "U", not an "L"); `triangle` now
  has all three sides (vertical leg, base, hypotenuse), with hypotenuse
  node ids returned to the caller for the interference variant. Exact
  proportions (arm/side length split) are a documented modeling choice,
  not derived from the paper's own coordinates (unpublished).
- **P1-3 (ping payload + size-dependent airtime).** `SetPingPayloadBytes`
  (default 100, matching Section 4) and `SetPhyTiming(bitrateBps,
  radioOverheadS)` added; when the latter is called, `ChargeEnergy`
  computes duration from `8*frameBytes/bitrate + radioOverheadS` instead
  of the old size-independent fixed slot constant, so payload size now
  genuinely affects the energy total.
- **P1-4 (NET_SCAN/CONNECT phases).** `StartConnectionPhase` broadcasts a
  NET_SCAN then a CONNECT announcement before path discovery, giving
  `energyByType` non-zero entries for both (needed for a Fig. 8-style
  breakdown). Exact NET_SCAN/CONNECT payload content is unspecified by
  the paper; both are minimal (header-only) by design choice.
- **P2-1 (per-type energy export).** CSV now includes one
  `energy_<frameType>` column per `FrameType` (netScan, connect,
  pathDiscovery, pathDiscoveryReply, ping, pingReply, ack), summed across
  all nodes per run.
- **P2-2 (statistics).** `analyze-results.py` now uses a t-distribution
  critical value (table for df 1-29, falling back to z=1.96 beyond that)
  instead of a fixed z=1.96, and additionally outputs
  `summary_ratios.csv`: each metric's mean energy as a percentage of the
  three-metric mean for that (layout, envFactor) cell, with a
  Best/Average/Worst reachability bucket assigned by envFactor terciles
  per layout (mirroring Fig. 7's presentation), plus the interference
  integrity check described under P0-3.
- **P2-3 (scenario-grid honesty).** See the run-count note at the top of
  `leo-topologies.cc`: the paper's own text is arithmetically
  inconsistent about total run counts (24 files x 3 metrics x 20 runs =
  1440, but the same paragraph also states 1920). This driver does not
  attempt to force either number -- choose and report your own
  (layout x envFactor) grid and `--runs` value explicitly.
- **P3 items (unit tests, dual-ns3-version CI, per-(target,seq) timeout
  map, author correspondence)** are recommended but not implemented here;
  track them separately if you pursue full engineering hardening.

### Equation/algorithm live-status table (P0-4)

| Symbol | Status | Where |
|---|---|---|
| Eq. (1) (ZigBee generic cost) | LIVE (primary LQI path) | `ZigbeeLqiMetric::ComputeLinkMetric` |
| Eq. (2)-(3) (nRF52840 LQI) | SUBSTITUTED (degenerates under realistic spacing; kept as static helpers) | `LqiFromRssiDbm`/`LinkCostFromLqi` |
| Eq. (6)-(10) (ATPC) | LIVE (both feedback modes) | `HandleAck`, `OnReceive`, `SendUnicastReliable` |
| Eq. (11) additive route weight | LIVE (inlined); `IRouteMetric::CombineRoute` is DEAD (never called) | `wsn-routing-app.cc` |
| Eq. (12)-(13) one-way-link rejection | LIVE (as of P1-1) | `LeoMetric::IsBidirectional` |
| Eq. (14)-(17) LEO cost | LIVE | `LeoMetric` |
| Eq. (15) dBm<->mW | `DbmToMw` LIVE; `MwToDbm` DEAD (unused) | `route-metrics.cc` |
| Eq. (18) path loss | LIVE | `LinkSignalLossDb` |
| Fig. 2 PRR(SNR) | LIVE, digitized from Fig. 2 (Sept 2026, see `data/fig2-digitized/`) | `kPrrCurve` |
| Eq. (2)-(3) literal, `--metric=lqi-literal` | LIVE (as of a third review round), no correction applied -- exposes a verified directional inconsistency (stronger links get HIGHER cost) in addition to the floor-effect degeneracy already documented for `ZigbeeLqiMetric` | `ZigbeeLqiLiteralMetric` |
| Algorithm 1 & 2 | LIVE (as of P1-1) | `atpc.cc` |

## v1.1.0 R2 radio/physical/energy requalification

The repair branch changes several semantics that materially affect any
future raw dataset:

- **TX power is a physical property, not an energy-model switch.** Requested
  powers are always realized on the nRF52840's supported discrete TXPOWER
  levels. A request such as 3.2 dBm therefore becomes +4 dBm whether
  `useNrf52840Energy` is true or false.
- **The LEO metric uses the same realized `P_TX` as the channel.** It no
  longer scores a continuous requested power while the hardware transmits
  a different discrete value.
- **Failed reception attempts consume RX energy.** A frame that fails the
  PRR/CRC outcome is not delivered to routing/ATPC state, but the intended
  unicast receiver (or broadcast listener) is charged the corresponding
  frame-duration RX energy.
- **Missing ACKs consume listening energy.** An ACK timeout charges the
  sender for the configured `ackTimeoutS` receive/listen window; a
  successfully received ACK is already charged as its decoded frame.
- **Effective SNR is preserved.** `NeighborEntry::lastSnrDb` stores the
  channel's post-interference SNR, preventing the LQI first-contact
  fallback from silently reconstructing an unpenalized SNR from RSSI.
- **Eq. (17) policy is explicit.** `boundEq17ToRMax=true` reproduces the
  historical v1.0.0 clamp; `false` evaluates the transcribed equation
  literally. For `R_max=4` and a 15 dB power deficiency, those policies
  give `R_D=4` and `R_D=40`, respectively. The final production choice
  is an experiment-freeze/source-fidelity decision, not a hidden code choice.
- **Finite ARQ remains an operational reconstruction of the reliability
  process**, not a claim that the implementation is algebraically
  identical to the paper's Eq. (4). `macMaxRetries`, `ackTimeoutS`, and
  `discoveryTimeoutS` are now CLI-exposed so R4 can freeze and sweep them.

The exact source-paper justification for the numeric interference penalty,
the operational ARQ substitution, and whether Eq. (17) should be bounded
is still treated as **SOURCE_FIDELITY_PENDING** until an independently
verifiable full-text source/author clarification is available.

## Third-round addition: datasheet-anchored nRF52840 energy model (opt-in)

Following a lighter external review's suggestion (and cross-checked
against R3C2_DESIGN_PREVIEW.md's stated plan to avoid an invented
current model), `contrib/leo-wsn/model/nrf52840-current-table.h` adds a
discrete-TX-power, datasheet-anchored current model as an **opt-in**
alternative to the original continuous `DbmToMw(dbm)` / fixed
`rxPowerPenaltyMw` model:

- **Directly sourced from the official nRF52840 Product Specification
  v1.11, Section 6.20.15.2/6.20.15.3** ("Radio current consumption",
  RADIO-peripheral-only, DC/DC regulator, 3 V), confirmed by direct text
  extraction from the document (not a secondhand citation): **nine of the
  fourteen discrete TX levels** (-40, -20, -16, -12, -8, -4, 0, +4,
  +8 dBm) and **both RX bitrates** (4.6 mA @ 1 Mbit/s, 5.2 mA @ 2 Mbit/s)
  are directly quoted. Only the remaining five TX levels (+2, +3, +5, +6,
  +7 dBm) are linearly interpolated. An earlier revision of this table
  used Section 5.2.1.5's SYSTEM-level figures instead (which additionally
  include HFXO clock current, hence read higher -- e.g. 6.40 mA vs 4.8 mA
  at 0 dBm) and had only 3 official TX anchors; Section 6.20.15 was
  adopted instead because it is far more complete, at the cost of
  excluding clock current (documented explicitly in the file header) --
  so this table under-estimates a real deployed node's *total* draw and
  should be understood as RADIO-peripheral-only current. A DevZone thread
  (Case ID 280167) independently confirms both figures are legitimate,
  measuring different things.
- The nRF52840's TX power is **discrete**, not continuous -- 14 fixed
  levels (-40, -20, -16, -12, -8, -4, 0, +2, +3, +4, +5, +6, +7, +8 dBm),
  confirmed via Nordic DevZone and consistent with the paper's own stated
  range (Section 3). `SnapToNearestAvailableTxPowerDbm()` now snaps any
  requested TX power to the nearest available level >= the request,
  matching the paper's own description ("P_TX is set to the nearest
  greater or equal available transmission power", Section 3.3) --
  previously the code used the requested dBm value directly regardless of
  whether real hardware could produce it.
- Current at every OTHER level in the table is **linearly interpolated**
  between the nearest known/anchor points, and is explicitly marked
  `officialSource = false`. This is a declared, disclosed approximation
  (per the LeapSpace review's guidance), not a claim of datasheet-grade
  accuracy at every level.
- `RadioParameters::useNrf52840Energy` / `--useNrf52840Energy` now
  controls **energy accounting only**. Physical TX realization is always
  discrete for the modeled nRF52840, through the single authoritative
  `RealizeNrf52840TxPowerDbm()` path used by data, ACK, channel RSSI/PRR,
  and the LEO metric's `P_TX` term. This intentionally breaks v1.0.0's
  coupling where disabling the current model also allowed physically
  impossible continuous TX levels. Consequently, v1.0.0 raw CSVs are
  historical artifacts and must be regenerated after the v1.1.0 repair;
  they cannot be "patched" into equivalence.
- **HFXO clock current can now be added back in (opt-in), sourced from
  the same document, Section 5.4.4.2.** The table's RADIO-only current
  excludes the current the HFXO crystal oscillator itself draws while
  running -- without it the radio cannot actually operate, so total node
  current is (table current) + (HFXO standby current). Five real crystal
  part numbers' standby currents are available via
  `Nrf52840HfxoStandbyCurrentMa()` (70-143 uA depending on crystal); set
  `RadioParameters::hfxoStandbyCurrentMa` (or `--hfxoCrystal=...` on the
  command line) to add one in. Default is 0.0 (no clock current added),
  preserving the RADIO-only scope described above. The paper does not
  specify which crystal was used, so picking one is a declared
  assumption you must report if you use it. NOT modeled: the ~330-830 uA
  startup transient during the first 1 ms after the crystal powers up
  from a fully off state -- adding that would require tracking HFXO
  power state transitions across the simulation, which this module does
  not do; if your deployment duty-cycles the crystal off between
  transmissions, real energy will be somewhat higher than this model
  predicts.
- Read the file-level comment in `nrf52840-current-table.h` before citing
  any absolute number produced with this mode enabled -- it lists every
  caveat (RADIO-peripheral-only, excludes HFXO clock current; five of
  fourteen TX levels still interpolated; fixed 3.0 V supply assumption)
  that a reviewer would reasonably ask about.

Two small defensive-programming guards were added at the same time
(cheap, low-risk, suggested by the same lighter review):
- `LinkSignalLossDb` now clamps at 0 dB, since for sub-1-meter distances
  the literal Eq. (18) formula can otherwise produce a positive value
  (implying propagation *amplifies* the signal), which is not physically
  meaningful.
- `ChargeEnergy` now asserts (`NS_ABORT_MSG_IF`) that every computed
  per-frame energy value is finite and non-negative, failing loudly
  rather than silently accumulating a corrupted value into a run's
  totals.

## Fourth-round addition: literal ZigBee LQI (Eq. 2-3) as a separate, labeled variant

Following a third external review round, `--metric=lqi-literal`
(`ZigbeeLqiLiteralMetric`) applies the nRF52840-specific Eq. (2)-(3)
**exactly as published, with no correction of any kind**, instead of
silently substituting the generic Eq. (1) (which `--metric=lqi` still
uses as its primary path, per the second-round finding that Eq. (2)-(3)
degenerates to a floor of 1.0 -- i.e. numerically identical to Hop-count
-- at any realistic deployment spacing).

**A second, sharper finding surfaced while implementing this**: Eq. (2)-(3)
has a directional inconsistency independent of the floor effect. Worked
example: RSSI=-80 dBm (a weak link) gives LQI=12, C_l=1.0; RSSI=-50 dBm (a
strong link) gives LQI=42, C_l=1.3125 -- the physically better link gets
the higher cost, so a "lowest-cost-wins" router (as everything in this
module is) would systematically prefer the weaker link whenever the floor
doesn't mask it. This traces to an inconsistency in the paper itself:
Section 2 states "LQI can range from 0 (best quality) to 255 (worst
quality)", but Eq. (3) (LQI = RSSI + 92) is an *increasing* function of
signal strength, so stronger signals produce numerically higher LQI --
which under the paper's own "0=best" convention should map to *lower*
cost, not higher as Eq. (2) actually computes.

**What this project does NOT do:** invent a "corrected" reverse-mapped
variant and call it a fix. Determining the *correct* mapping would
require either the authors' clarification or nRF52840/IEEE 802.15.4 LQI
computation details beyond what the paper itself specifies, and picking
one unilaterally risks smuggling in exactly the kind of undisclosed
"baseline redefinition" this whole review process has been trying to
prevent. If you want a corrected variant, treat its derivation as an open
methodological question to resolve and document explicitly -- not as a
drop-in code change -- before implementing and clearly labeling it (e.g.
`lqi-corrected`, never as "ZigBee LQI as specified in the paper").

Recommended use: run `hopcount`, `lqi` (Eq. 1), `lqi-literal` (Eq. 2-3
verbatim), and `leo` side by side. If LEO's advantage holds against both
LQI variants, that conclusion is considerably stronger than if it only
holds against one particular interpretation of the ZigBee baseline.

## Fifth-round addition: node mobility (two-model methodology)

Following a methodology discussion on how to study the LEO metric under
node mobility in a publication-defensible way, `leo-topologies.cc` now
supports `--mobility=none|walk|waypoint` (default `none`, reproducing all
prior static-node behavior unchanged):

- **`walk`**: `RandomWalk2dMobilityModel`, bounded to a small square box
  **centered on each node's own Fig. 4 layout position** (half-width =
  `--mobilityBoxFrac` x `spacingM`, default 0.3), speed drawn from
  `[--mobilitySpeedMin, --mobilitySpeedMax]` m/s (default 0.5-2.0),
  direction/speed re-picked every 2 s. This preserves the macro-topology
  under study (the six Fig. 4 shapes) while adding realistic local
  drift/jitter -- the condition most consistent with the source paper's
  fixed-WSN-deployment framing.
- **`waypoint`**: `RandomWaypointMobilityModel` over the full layout
  bounding box plus one `spacingM` margin, with `--mobilitySpeedMin`
  **enforced strictly > 0** (`NS_ABORT_MSG_IF` guard) to avoid the
  well-documented ns-3/RandomWaypoint average-speed decay artifact (Yoon,
  Liu & Noble, "Random waypoint considered harmful," INFOCOM 2003), pause
  time drawn from `[--mobilityPauseMin, --mobilityPauseMax]` s. This is an
  unconstrained, MANET-style stress test; under this mode the six Fig. 4
  layouts define only the *initial* topology, not an invariant maintained
  through the run -- report it as such.
- **`--mobilityWarmupS`**: simulated time discarded before the connection
  phase begins (both the connection-phase and ping-phase start times are
  shifted by this amount), letting the mobility process reach a
  statistically steady state before any measured traffic starts.
  Recommended > 0 for `waypoint` mode specifically (a standard requirement
  in the mobility-model literature, not optional for publication-grade
  RWP results); optional for `walk` mode, which has no equivalent
  long-transient concern.
- **`--mobilityDiagnostics=true`**: pre-flight validation, NOT part of the
  measured experiment. Periodically (every `--mobilityDiagnosticsPeriodS`
  seconds) computes and logs average node degree (fraction of node pairs
  with PRR > 0.5) to `--mobilityDiagCsv`. Run this BEFORE committing to a
  full sweep and check: (walk mode) degree stays broadly comparable to the
  static baseline and does not collapse toward zero or spike toward full
  mesh; (waypoint mode) degree does not visibly trend/decay over time
  after the warm-up period (a trend would indicate the warm-up was too
  short or a speed-decay artifact is still present).

**What this does NOT do**: this is a from-scratch implementation added in
this round; no mobility-condition results have been collected or
validated in this repository yet. Run the diagnostics check first, then a
small-scale sweep, before trusting any full-scale mobility results for
publication.

## Sixth-round addition: pre-publication test checklist (how to actually run it)

Seven tests, in the order recommended before treating results as
publication-ready:

1. **Equation-level unit oracle — two parts, both required.**

   Part A (Python oracle vs. hand-derived values):
   ```bash
   python3 equation_oracle.py
   ```
   Independent Python re-implementation of Eq. (1)-(3), (14)-(17),
   cross-checked against hand-derived expected values (13 test vectors,
   including the Fourth-round directional-inconsistency worked example).
   Must print `13/13 checks passed`.

   Part B (Python oracle vs. the REAL C++ — **this is the part that
   actually validates the shipped code**; Part A alone only proves the
   oracle matches hand arithmetic, not that `route-metrics.cc` agrees
   with either):
   ```bash
   ./ns3 run leo-equation-check > cpp_out.txt
   python3 equation_oracle.py --emit-values > py_out.txt
   diff cpp_out.txt py_out.txt && echo "ORACLE AND C++ AGREE"
   ```
   `scratch/leo-equation-check.cc` calls the same public static functions
   the simulation itself uses (`ZigbeeLqiMetric::LinkCostFrom*`,
   `LeoMetric::RetransmissionsFromPowerDeficiency`,
   `LeoMetric::AverageRetransmissions`, `LeoMetric::LinkPowerMw`,
   `DbmToMw`) on identical inputs, printing an identical `name=value`
   format. Any diff output is a real implementation discrepancy between
   the two and must be resolved before trusting any simulation result.
   Note: `./ns3 run leo-equation-check` may emit ns-3 build chatter on
   stdout in some configurations -- if `diff` reports spurious
   differences, filter with `grep '^eq' cpp_out.txt > cpp_clean.txt`
   first.
2. **Small-scale sanity run** before any full sweep:
   ```bash
   ./ns3 run "leo-topologies --layout=triangle --envFactor=2 --metric=leo --runs=5"
   ```
   Check: `pingSent+pingTimeouts+pingNoRoute` matches the expected total
   (targets x 10), no NaN/negative values, `energy_ack` is not
   disproportionately large compared to `energy_ping`+`energy_pingReply`.
3. **Mobility diagnostics** (only if using `--mobility=walk` or
   `waypoint`):
   ```bash
   ./ns3 run "leo-topologies --layout=circle --mobility=walk --mobilityDiagnostics=true --runs=1"
   ./ns3 run "leo-topologies --layout=circle --mobility=waypoint --mobilityWarmupS=60 --mobilityDiagnostics=true --runs=1"
   ```
   Inspect `mobility-diagnostics.csv`: `avgNodeDegree` should stay broadly
   comparable to the static baseline for `walk`, and should not visibly
   trend/decay over `simTimeS` for `waypoint` after the warm-up period.
4. **Statistical significance** (after collecting `leo-results.csv` via
   `run-experiments.sh`):
   ```bash
   python3 significance_test.py leo-results.csv --baseline leo --alpha 0.05
   ```
   Produces `significance.csv`: Welch's t-test (unequal-variance) p-value
   per (layout, envFactor) comparing LEO against every other metric
   present (hopcount, lqi, lqi-literal). Report the p-value and percent
   difference for every comparison, not just the ones that favor LEO.
5. **Sensitivity analysis** on undocumented constants, now CLI-exposed:
   ```bash
   for alpha in 0.1 0.2 0.3 0.5; do
     ./ns3 run "leo-topologies --layout=circle --envFactor=2.5 --metric=leo --emaAlpha=${alpha} --runs=10 --outCsv=sens_ema.csv"
   done
   for win in 0.05 0.15 0.30; do
     ./ns3 run "leo-topologies --layout=circle --envFactor=2.5 --metric=leo --discoveryWindowS=${win} --runs=10 --outCsv=sens_window.csv"
   done
   ```
   Also sweep `--backgroundNoiseDbm` and `--interferenceDb` the same way
   (already CLI-exposed from earlier rounds). Confirm LEO's qualitative
   ranking versus the baselines does not flip across the tested range.
6. **Independent-seed reproducibility**:
   ```bash
   ./ns3 run "leo-topologies --layout=circle --envFactor=2.5 --metric=leo --seedBase=42 --runs=20 --outCsv=seed42.csv"
   ```
   Re-run `analyze-results.py`/`significance_test.py` on the new seed and
   confirm the same qualitative conclusion holds (not bit-identical
   numbers -- a directionally consistent result).
7. **Qualitative comparison against the source paper's Figs. 5-7** --
   manual: no script, compare trend direction (does LEO show lower
   energy/timeouts than Hop-count/LQI in the same relative pattern the
   paper reports?) and note any layout/envFactor combination where the
   direction disagrees, rather than only reporting agreements.

## What this code does *not* claim



This module reproduces the paper's **equations** faithfully and gives you
a working harness to reproduce its **experimental design** (six layouts,
sweep of environmental factor, 20 repetitions per cell, gateway-ping
methodology, energy/timeout statistics). It does **not** claim to
reproduce the paper's *absolute* published numbers (e.g. the exact mWs
values in Fig. 5), because several inputs needed for that (exact node
coordinates, MAC timing constants, discrete-TX-power current draw beyond
what Section 6.20.15 documents) are not published in the paper or not
fully covered by public datasheets. The PRR/SNR curve (Fig. 2) is now a
direct digitization rather than an approximation (see item 1 above), but
remains one independent digitization with inherent pixel-level error, not
the authors' own raw data. Any comparison to the published figures should
be reported as a qualitative/trend-level reproduction unless you
substitute the authors' own raw data for the remaining items above.
