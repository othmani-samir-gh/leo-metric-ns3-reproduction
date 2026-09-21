# v1.1.0 release assets

The regenerated v1.1.0 dataset is distributed as a separate checksum-bound release asset rather than committed into the Git source tree.

## Dataset asset

File: leo-v1.1.0-results.tar.gz
SHA256: e32b25adcca49abcbe732ba75d80390d07542128395fca5332af3235cded3783
Compressed size: 13,829,224 bytes
Local source directory before packaging: results/v1.1.0/
Frozen configurations: 617
Simulation rows: 12,340
Route-evidence rows: 2,344,600
Final PASS receipts: 617
Pending route records: 0

The archive contains R5_INTEGRITY.json, PROVENANCE.json, CHECKSUMS.sha256, all configuration-level main.csv, routes.csv, run.log and receipt.json files, the R6 analysis outputs, and the preserved recovery record.

Verification sequence:

1. Verify the archive against leo-v1.1.0-results.tar.gz.sha256.
2. Extract leo-v1.1.0-results.tar.gz at the repository root.
3. Run sha256sum -c results/v1.1.0/CHECKSUMS.sha256.

The asset is prepared locally but still requires explicit human upload to the chosen release/archive destination.
