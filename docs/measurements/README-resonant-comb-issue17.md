# Resonant comb evidence for issue 17

The repository includes compact reports and independently audited summaries:

- [Numerical capture manifest](resonant_comb_issue17_final/result.json)
- [Numerical audit](resonant_comb_issue17_final_audit.json)
- [CPU capture manifest](resonant_comb_cpu_issue17_reference/result.json)
- [CPU summary](resonant_comb_cpu_issue17_reference/summary.csv)
- [CPU audit](resonant_comb_cpu_issue17_reference_audit.json)

Raw `.f32` impulses, source/rendered WAV files, CPU `timings.csv` and
`quiet_decay.csv` remain local generated artifacts and are excluded from this
commit. The manifests retain their hashes and the original executable/source
identities. Auditing an original capture requires those raw artifacts and the
matching executable; rebuilding may change executable identity. Use fresh
destinations to reproduce captures and audits with the commands in
[BUILDING](../BUILDING.md) and [PERFORMANCE](../PERFORMANCE.md).

The [validation note](../okf/notes/resonant_comb_validation.md) records settings,
limits, results and environment. Human listening and manual GUI appearance
acceptance remain pending. Production-chain integration is deferred to X5.
