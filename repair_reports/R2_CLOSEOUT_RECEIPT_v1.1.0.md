# R2 Closeout Receipt — LEO v1.1.0

Gate: `R2 — Radio / Physical / Energy Requalification`
Status: `PASS — CLOSED`

Detailed user artifact:
- filename: `R2_RADIO_PHYSICAL_ENERGY_REQUALIFICATION_v1.1.0.md`
- SHA256: `114f91518933506e3b7ef393a09bbd22681d8eabb3f9144216985ba225aba5fb`
- generated as the downloadable conversation artifact for this stage.

Repository state before this receipt:
- branch: `repair/v1.1.0`
- head: `fda283bd57842bbbc8e6e48669a0ff8b1aaffd79`
- tree: `1cdfbd6d30133bcc2fd56ec2ca077a0621e5cfe9`
- R1 closeout: `088834eb60d0cc76e3ae2abb3150725a9b15c579`

Qualification environment:
- ns-3.48 commit: `d2add90b452d600cfb4859baed8e9ea633519447`
- g++ 13.3.0
- CMake 3.28.3

Authoritative combined regression: `16/16 PASS`, `FINAL_RC=0`.

R2 closures:
- physical nRF52840 TX quantization is independent of energy-accounting mode;
- LEO metric uses the same realized TX power as the physical channel;
- ACK TX follows the same discrete TX realization rule;
- failed reception attempts consume RX energy without protocol delivery;
- missing ACK windows consume receiver/listening energy;
- first-contact LQI uses the channel's effective post-interference SNR;
- Eq.17 bounded-vs-literal policy is explicit and test-covered.

Executable qualification:
- Python vs C++ equation oracle: `PASS`;
- repaired `leo-topologies.cc` compiled against ns-3.48;
- new CLI controls visible in `--help`:
  `macMaxRetries`, `ackTimeoutS`, `discoveryTimeoutS`,
  `boundEq17ToRMax`, `radioOverheadS`, `useNrf52840Energy`;
- qualification-only 2-run smoke: `RC=0`, header + 2 rows.

Source-fidelity status:
- correct DOI: `10.1016/j.iot.2024.101472`;
- publisher surface identifies the paper as Open Access;
- direct full-text fetch returned HTTP 403 during R2;
- exact interference strength/PER setup, Eq.4 operational interpretation,
  and Eq.17 bounding intent remain `SOURCE_FIDELITY_PENDING`.

Historical raw CSVs remain untouched and historical-only.

Scientific data policy:
- patching old CSVs is forbidden;
- treating old CSVs as v1.1.0 is forbidden;
- full raw regeneration is required only after R3/R4 closure.

ASTRA status:
- NOT RUN: no callable ASTRA provider/binding was available;
- no substitute reviewer was mislabeled as ASTRA.

Gate transition:
`R2 = PASS`
`R2 = CLOSED`
`R3 = OPEN`
`SCIENTIFIC_RERUN = CLOSED`

Next allowed action:
`R3 — RNG / Robustness / Runtime Requalification`, starting with
deterministic RNG stream identity and common-random-number pairing.
