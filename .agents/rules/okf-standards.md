---
trigger: always_on
description: "Enforce Open Knowledge Format standards for research documents under docs/okf."
---

# Open Knowledge Format (OKF) Rules

## 1. Conformance Rules
- All conceptual documents under `docs/okf/` MUST be valid Markdown with YAML frontmatter bounded by `---`.
- OKF Core Requirement: Every concept document MUST define the `type` field in its frontmatter.
- Recommended frontmatter fields:
  - `type`: String (`Paper`, `Method`, `Hardware`, `Project`, `Note`, `Index`, `RootIndex`, `Log`).
  - `title`: Human-readable title of the document.
  - `description`: 1-2 sentence executive summary of the content.
  - `timestamp`: ISO 8601 UTC timestamp of creation/revision (e.g. `2026-09-03T16:00:00Z`).
  - `tags`: List of kebab-case tags (e.g. `["3dgs", "autonomous-driving"]`).
  - `resource`: URL or external reference link (e.g. arXiv URL, GitHub repo, manufacturer doc).
  - For papers: `authors` (list of strings), `arxiv_id`, `published` (YYYY-MM-DD).

## 2. File Organization
- Knowledge items MUST live in their appropriate category directory under `docs/okf/`: `papers/`, `methods/`, `hardware/`, `projects/`, or `notes/`.
- Concept filenames MUST use snake_case slugs (e.g., `papers/gaussian_dwm.md`). Do not name files solely by numerical arXiv IDs.
- Each category directory MUST contain an `index.md` listing its concepts.
- `docs/okf/` MUST contain `index.md` (root hub) and `log.md` (chronological changelog).

## 3. Linking & Graph Conventions
- Cross-references between concepts MUST use relative Markdown links:
  - Same directory: `[StreamSplat](./streamsplat.md)`
  - Sibling directory: `[Jetson AGX Orin](../hardware/jetson_agx_orin.md)`
- Avoid broken links. Run `uv run --no-project --python 3.12 scripts/okf_manager.py --validate` to verify all links before concluding tasks.

## 4. Automatic Maintenance
- After creating, moving, or editing any concept:
  1. Update indices and append an entry: `uv run --no-project --python 3.12 scripts/okf_manager.py --update-indices --log "<Summary of changes>" --validate`.
  2. In a restricted Windows sandbox, set `UV_CACHE_DIR` to a writable temporary directory before running `uv`.
