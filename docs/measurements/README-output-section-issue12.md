# Issue 12 output-section evidence

The qualification procedure and measured results are in
[the output-section note](../okf/notes/output_section_validation.md) and
[the performance guide](../PERFORMANCE.md).

## Published artifacts

- `output_section_issue12_retry1/` and `output_section_issue12_repeat/` retain
  the complete numerical CSV summaries, original result reports and manifests.
  Artifact hashes matched between the two complete local runs.
- `output_section_examples_issue12/{wide,narrow,crossed}/result.json` retains
  the exact settings and measured outcomes of the three example renders.
- `output_section_cpu_issue12_reference.csv` records the full three-run,
  600-case pinned AC stereo matrix. Its `_reference_audit.json` records the
  independent raw-data audit.
- Stereo and mono `_diagnostic.csv` files and `_audit_verified.json` reports
  retain the separate quick, unpinned evidence. Those runs are unqualified.

Generated source/wet WAVs, fixture subdirectories and large `.timings.csv`
sidecars remain local. The manifests and CPU audits retain their hashes.
Scoped Git attributes preserve the exact evidence bytes, including original
CRLF endings, so the committed CSV/JSON files retain those recorded identities.
The incomplete first numerical attempt and earlier CPU audits remain local
and are excluded from acceptance. The committed compact artifacts alone cannot
re-run validators that require those local WAVs or raw timing distributions.

## Reproduction and identity

Build the documented Release targets, generate measurements into new
directories, and run the independent validators on those complete outputs.
The numerical validator checks WAV completeness and repeat/corruption controls;
the CPU validator's `--reference --runs 3 --channels 2` mode checks the full
reference matrix and raw timings. See the linked guides for exact commands.
A fresh CPU run produces a new distribution and hash.

Original reports identify the uncommitted implementation through source and
executable SHA256 values plus base Git revision `0c5c0ca`. That revision alone
does not identify the implementation. The numerical tool's original
`referenceHardwareQualified: false` describes its own evidence; the separate
later CPU audit is the source for CPU qualification. Generated fixtures and
successful builds do not constitute structured human listening or visual GUI
inspection.
