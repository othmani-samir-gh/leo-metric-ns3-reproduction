# LEO ns-3 reconstruction v1.1.0

## Scope

v1.1.0 is a repaired and requalified release candidate. It preserves v1.0.0 as historical provenance while replacing the authoritative simulator, dataset, statistical analysis, figures, and release claims.

## Major repairs

- Eq.12–13 link usability is enforced before route mutation.
- Discovery and PING transactions are identity-safe and have explicit liveness handling.
- Final ARQ failure invalidates dependent routes.
- Cross-run frame state is reset and device/channel ownership cycles are removed.
- Receiver callbacks execute in the correct ns-3 node context.
- nRF52840 physical TX levels are discrete independently of energy-accounting mode.
- LEO metric P_TX and physical transmitted P_TX use the same realized value.
- Failed RX attempts and missing-ACK listening consume energy.
- Effective post-interference SNR is preserved for first-contact LQI.
- RNG streams are explicitly assigned and deterministic across static and mobility modes.
- Unsafe CLI/configuration states fail closed.
- Experiment design, provenance, route evidence, and relay-cap identity are machine-readable and frozen.

## Regenerated dataset

The frozen v1.1.0 plan contains 617 configurations:

- 420 primary static source-constrained configurations
- 36 Eq.17 bounded-policy sensitivity configurations
- 72 nRF52840 energy-accounting sensitivity configurations
- 17 reconstruction OFAT sensitivity configurations
- 72 exploratory mobility configurations

Final data volume:

- 12,340 simulation rows
- 2,344,600 route-evidence rows
- 617 PASS receipts
- 0 pending route records

Dataset archive:

- leo-v1.1.0-results.tar.gz
- SHA256: e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783

See RELEASE_ASSETS.md and RELEASE_ASSET_MANIFEST.json.

## Statistical reanalysis

Primary confirmatory inference is restricted to the source-constrained static design.

Compared with hop-count, LEO reduces ping timeouts and no-route outcomes in all non-interference relay-cap strata. The energy effect is relay-cap dependent: a significant energy reduction is supported at relayCap=1, while cap=2 and cap=3 do not support a significant energy reduction.

Compared with the operational LQI comparator, LEO uses more energy in all three non-interference relay-cap strata.

The literal LQI formulation is exactly degenerate with hop-count in the complete static primary dataset.

Interference strata contain only five scientific cells per stratum and are treated as limited-evidence sensitivity results.

Mobility results are exploratory and are not pooled into the primary source-constrained estimand.

## Reproducibility

- R1–R4 regression suites: 20/20 PASS
- R5 global integrity: PASS
- R6 deterministic statistical rerun: PASS
- R7 deterministic figure rebuild: PASS
- R8 equation oracle: PASS
- R8 deterministic replay in none/walk/waypoint: PASS
- R8 ASan/LeakSanitizer/UBSan smoke qualification: PASS
- R8 claim-to-data consistency: PASS

## Claim boundary

This release does not claim:

- universal LEO energy superiority;
- generic superiority over operational LQI;
- confirmatory interference effects;
- exact replay of the unpublished original simulator code;
- mobility as part of primary source-reproduction inference.

## Human release actions

Before publishing the v1.1.0 tag/release:

1. Confirm the final author list in CITATION.cff.
2. Upload leo-v1.1.0-results.tar.gz and verify its SHA256 at the destination.
3. Create the v1.1.0 tag/release from the final approved commit.
4. Optionally add continuous integration for automated future regression checks.
