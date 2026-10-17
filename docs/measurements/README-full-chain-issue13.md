# Issue 13 full chain evidence

The compact `full_chain_issue13_summary.json` records test counts, repeat
identity, numerical limits, measured latency and example render levels.
`full_chain_issue13_audit.json` and `full_chain_issue13_repeat_audit.json`
retain independent numerical audit details and complex blend projections.

The local directories `full_chain_issue13` and `full_chain_issue13_repeat`
contain 272 multi-lane float captures, four additional wet-latency impulse
captures and a manifest with exact settings and source/executable SHA256
identities. Both manifests and all capture hashes matched between runs.
Large binary captures are reproducible local evidence; retain compact reports
and provenance with publication rather than adding every capture to Git.

`full_chain_examples_issue13` holds playable source/rendered WAVs and result
records for the gentle, destructive and phase-blend examples under
`examples/experiments`. Each render has 145024 frames and 92 samples of
leading latency, with no automatic compensation or tail flush. Human listening
is pending; no sound verdict is inferred from their successful rendering.

`full_chain_cpu_issue13_pinned` stores the completed Release diagnostic's raw
timings, summary and provenance. All 120 cases and 120000 raw timings passed
independent auditing; worst p99 was 6.15% of the incoming deadline (25% limit).
The local launch helper observed highest efficiency class 1, group 0 mask 0x1,
HIGH process priority and AC power before/after. `environment.json` retains
those observations; affinity was assigned immediately after spawn before the
20 ms warmup completed. This quick diagnostic keeps 1000 timings per case.
Thermal conditions and normal OS services were uncontrolled. Reference CPU
qualification requires the established pinned AC three-run adaptive procedure;
it is not claimed here.

The earlier `full_chain_cpu_issue13` directory is an **incomplete unpinned run**
stopped after observing p99 budget failures and slow collection. Its partial
CSV data are retained for diagnosis, excluded from acceptance and have no
completed manifest or independent passing audit. Do not publish it as a
complete qualification result.
Use `scripts/validate_full_chain_measurements.py` with the exact executable
and a fresh audit output to independently verify either measurement or CPU
artifacts. The validator verifies current source hashes as well.

See [design and qualification](../okf/notes/full_chain_validation.md) for
commands, checks and remaining acceptance gates. Do not mix an old executable
or changed source tree with these manifests; regenerate into fresh directories.
