---
name: okf-curator
description: Use when adding or maintaining research concepts, indexes, links, or metadata in the DisDorktion OKF bundle.
---

# OKF Curator Skill

Maintain the DisDorktion research bundle under `docs/okf/`.

## Responsibilities of the Curator
1. **Catalog Maintenance**: Ensure all conceptual files in `docs/okf/papers/`, `docs/okf/methods/`, `docs/okf/hardware/`, `docs/okf/projects/`, and `docs/okf/notes/` have valid YAML frontmatter.
2. **Graph Connectivity**: Weave semantic cross-links between concepts (e.g. methods link to papers; hardware links to projects; papers link to experiments).
3. **Index Refreshing**: Ensure category `index.md` files and `docs/okf/index.md` accurately summarize the bundle.
4. **Audit Trail**: Ensure changes are recorded in `docs/okf/log.md`.

## DisDorktion Content Guide

Use OKF to preserve the research and design reasoning behind the JUCE plugin. Keep detailed findings in concept pages; this skill is the checklist.

| Category | Capture |
| --- | --- |
| `projects/` | The plugin's intended sound, signal chain and module order, supported channel layouts, parameter overview, design decisions, and open questions. Start with `disdorktion.md`. |
| `methods/` | One page per DSP technique (such as demodulation, bit crushing, tanh shaping, wave folding, or comb filtering): algorithm, parameter ranges and endpoints, expected sound, aliasing and latency concerns, and alternatives considered. |
| `papers/` | Full source details, a concise summary, the specific finding relevant to the plugin, and links to the methods it informs. |
| `notes/` | Experiments and listening results: test signal, sample rate, settings, observations or measurements, and the resulting decision. |
| `hardware/` | Source-backed details of hardware being studied or emulated, when applicable. |

For DSP claims, distinguish **proposed behavior** from **measured behavior**. Record how a proposal was evaluated and whether the evidence led to acceptance or revision. Cross-link project, method, paper, hardware, and experiment pages where they inform one another.

---

## Standard Procedures

### Adding a New Concept
1. Determine the category folder (`methods/`, `hardware/`, `projects/`, or `notes/`).
2. Create `docs/okf/<category>/<snake_case_slug>.md`.
3. Add required frontmatter:
   ```yaml
   ---
   type: Method # or Paper, Hardware, Project, Note
   title: "Concept Title"
   description: "One or two sentences explaining what this concept is and why it matters."
   timestamp: 2026-10-03T00:00:00Z
   tags: ["relevant", "keywords"]
   resource: "https://source.example.org"
   ---
   ```
4. Write structured markdown body with headers, tables, code blocks, or diagrams.
5. Add links to related concepts:
   - Sibling folder: `[Related Paper](../papers/example.md)`
   - Same folder: `[Related Method](./other_method.md)`
6. Run maintenance:
   ```bash
   uv run --no-project --python 3.12 scripts/okf_manager.py --update-indices --log "Created concept: <Category>/<Slug>" --validate
   ```

### Health Check & Validation
Run validation anytime files are renamed, moved, or deleted:
```bash
uv run --no-project --python 3.12 scripts/okf_manager.py --validate
```
The script will report:
- Missing required OKF fields (`type`).
- Missing recommended fields (`title`, `description`).
- Broken internal links (links pointing to nonexistent files).

The manager uses only the Python standard library. In a restricted Windows sandbox, set `UV_CACHE_DIR` to a writable temporary directory before running `uv`.
