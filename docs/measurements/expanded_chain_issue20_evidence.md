# Issue 20 published qualification evidence

This change publishes the collector/auditor sources and compact JSON/CSV
reports. Raw timing CSVs, decay traces, float captures, golden manifests and
compiled executables remain in the originating checkout; they are excluded
from Git because they are large generated artifacts. Reports retain their
original hashes, machine paths and conditions. A fresh checkout can regenerate
evidence with the documented tools, but cannot independently replay these
historical raw audits from the committed reports alone.

- `expanded_chain_issue20_cpu_reference_highqos_v1/`: complete 180-case report,
  session and summary; the adjacent `_audit.json` independently confirms the
  three-run reference under `windows-explicit-highqos-v1`.
- `expanded_chain_issue20_cpu_reference_v2/`: original system-managed-policy
  failure, session and partial summary; its adjacent audit remains unqualified.
- `expanded_chain_issue20_stress_v2/` and
  `expanded_chain_issue20_stress_adaa_cache/`: before/after stress reports and
  adjacent independent raw-audit summaries, each covering 48 cases.
- `expanded_chain_cpu_adaa_cache_golden_audit.json`: exact before/after output
  comparison across 48 cases, with file hashes and sample layouts.
- `expanded_chain_policy_abba_*/result.json`: all four predeclared frozen-baseline
  default/HighQoS/HighQoS/default screens; short diagnostics, not qualification.
- `expanded_chain_profile_*/result.json`: retained baseline attribution and
  baseline/candidate policy screens; all explicitly unqualified.

The accepted CPU reference retains the 25% incoming-deadline ceiling,
100,000–200,000 observations per case, coverage, AC/profile, selected-core,
92-sample latency and raw-provenance requirements. Its worst p99 is 6.490746%
of deadline. Explicit policy is applied only to the standalone collector and
restored afterward; the plugin never changes its host's policy. The original
failure remains valid, and actual DAW validation is separate.

See [performance procedure](../PERFORMANCE.md) for commands and interpretation.
