# R8 — Final Independent Requalification

## Program
LEO v1.1.0 — Repair & Requalification

## Status
PASS_WITH_RELEASE_HUMAN_GATE — R8 CLOSED

## Release decision
TECHNICALLY_ELIGIBLE_FOR_v1.1.0_RELEASE_CANDIDATE

Public release is not yet authorized.

## Baseline
R7 closeout commit:
83ebffef0a48b5b31ee04c3a4907a9eef6b63636

R8 packaging commit:
82cd0fa0d9caf041d6226b6cd6447d8374afafad

Scientific execution source commit:
c95c9548bd790170697d3d35a401c618290c04dd

Scientific execution source tree:
a0f60011722f87f61710fe96ad54f1adf2047b8f

ns-3 commit:
d2add90b452d600cfb4859baed8e9ea633519447

R5 binary SHA256:
7422bd9951a645553edabe7335c62270a076516c130523f006ec96e42b1de5eb

## Final verification
R4 manifest validator: PASS
R4 plan validator: PASS
R1–R4 regression suites: 20/20 PASS
R5 checksum chain: 2478/2478 PASS
R6 checksum chain: 14/14 PASS
R7 checksum chain before R8 packaging edits: 23/23 PASS
Equation oracle: PASS
Deterministic replay none/walk/waypoint: PASS
ASan/LeakSanitizer/UBSan none/walk/waypoint: PASS
Claim-to-data consistency: 15/15 PASS
Scientific simulator code drift since R5 execution: NONE

## Literal LQI direct raw check
Hop-count / LQI-literal pairs: 105
Main rows compared: 2,100
Main mismatches: 0
Route rows compared: 399,000
Status/fingerprint/hop mismatches: 0

## Dataset release asset
Asset:
leo-v1.1.0-results.tar.gz

Local path:
/home/pharmaco/projects/LEO_v1.1.0_Release_Assets/leo-v1.1.0-results.tar.gz

SHA256:
e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783

Compressed size:
13,829,224 bytes

gzip integrity: PASS

The archive contains R5_INTEGRITY.json, PROVENANCE.json, CHECKSUMS.sha256 and the full generated dataset/evidence bundle.

## Release metadata
CITATION.cff version: 1.1.0
README release-asset distribution wording: corrected
RELEASE_ASSETS.md: present
RELEASE_ASSET_MANIFEST.json: present
RELEASE_NOTES_v1.1.0.md: present

Release notes SHA256:
762e2a2f4694f7d0ae8eedc140e6ae32e462cc3cb1005a18872df9a3f3e23ae7

## Final R8 QC
R8_FINAL_QC.json SHA256:
f5c9f752b2dba227470f7c16b240cc9387aa4ec4574db86165388fda6acd414a

R8_RELEASE_CANDIDATE_MANIFEST.json SHA256:
d4ee478d7b98e5d3000b77e925824ee95bc39ffc6f7a8f9574a1ccfdbf48ac7e

R8_CHECKSUMS.sha256 SHA256:
494200b2c42df26bb09726603be163c91f8a5641b297b299326acc9c76db26dc

R8 checksum verification:
37 entries
37 OK
0 failed
RC 0

## Remaining human gate
1. Confirm final author list in CITATION.cff.
2. Upload leo-v1.1.0-results.tar.gz.
3. Verify SHA256 after upload.
4. Create v1.1.0 tag/release only after those checks pass.

## Non-blocking recommendation
Add CI for future automated regression and validator checks.

## External review
ASTRA NOT RUN — callable binding unavailable.
No substitute reviewer was represented as ASTRA.

## Final scientific disposition
The repaired v1.1.0 chain is technically coherent and substantially stronger than v1.0.0.

Supported release interpretation:
- LEO materially changes routing relative to hop-count and operational LQI.
- Relative to hop-count, LEO reduces timeout and no-route outcomes across primary non-interference relay-cap strata.
- Confirmatory energy reduction versus hop-count is supported only at relayCap=1 in the primary design.
- LEO does not show generic energy superiority over operational LQI.
- Interference remains underpowered for confirmatory claims.
- Mobility remains exploratory.
- Reconstruction assumptions remain explicit limitations.
- Exact replay of the unpublished original simulator implementation is not claimed.

## Gate decision
R8 = PASS_WITH_RELEASE_HUMAN_GATE
R8 = CLOSED
TECHNICAL_RELEASE_CANDIDATE_ELIGIBLE = TRUE
PUBLIC_RELEASE_AUTHORIZED = FALSE

The technical/scientific requalification program is complete.

The next step is not R9.

The next step is the human release gate.
