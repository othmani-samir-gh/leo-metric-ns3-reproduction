# Digitized Fig. 2 data (Sept 2026, second-pass re-extraction)

Raw WebPlotDigitizer (automeris.io/wpd/) exports of the paper's Fig. 2
("Measurement results - dependence of PRR and PER on SNR"), digitized by
the user from the published figure. Each file is `SNR_dB, percent` (no
header row).

**This is the second-pass extraction**, re-done at much higher point
density (562/533/577 points vs. 95/~90/~90 in the first pass), with
particular attention to the steep 12-20 dB transition region (~6x denser
there specifically). Validated as more internally consistent than the
first pass: `OK% + N_reception% + CRC% ` deviates from the expected 100%
by a mean of 0.19 percentage points across SNR 0-79 dB in this version,
vs. 0.42 in the first pass (worst case 0.79 vs. 2.07 points) -- computed
by linearly interpolating all three curves onto a common 0.25 dB grid and
summing.

- `OK.csv` -- the blue "OK" (successful reception) curve. This is the one
  actually used in the code: `kPrrCurve` in
  `contrib/leo-wsn/model/wsn-channel.cc` is built directly from this file
  (562 points, SNR ~1.1-77.8 dB, near-duplicate x-values deduplicated,
  converted from percent to a [0,1] fraction, with a `{0.0, 0.0}` floor
  anchor prepended).
- `N_reception.csv` -- the orange "No reception" curve. Not currently
  consumed by any code path; kept here for anyone who wants to model the
  no-reception/CRC-error distinction separately from the aggregate PRR
  used today.
- `CRC.csv` -- the gray "CRC error" curve. Same status as
  `N_reception.csv`.

**Provenance caveat:** this is one independent digitization of a static
published chart, not the authors' own raw measurement data (which remain
unpublished). WebPlotDigitizer extraction inherently carries pixel-level
reading error, even at high point density. The consistency check above
indicates the digitization is self-consistent, not that it is
pixel-perfect. If you can obtain the paper authors' original measurement
data, prefer that over this digitization and update
`contrib/leo-wsn/model/wsn-channel.cc` accordingly.
