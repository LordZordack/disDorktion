#!/usr/bin/env python3
"""Maintain and validate the research bundle in docs/okf."""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1] / "docs" / "okf"
CATEGORIES = ("papers", "methods", "hardware", "projects", "notes")
LINK = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
FIELD = re.compile(r"^([A-Za-z_][A-Za-z_0-9]*):\s*(.*)$")


def now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def frontmatter(kind: str, title: str, description: str) -> str:
    fields = {
        "type": kind,
        "title": title,
        "description": description,
        "timestamp": now(),
    }
    if kind == "RootIndex":
        fields["okf_version"] = "0.2"
    return "---\n" + "\n".join(f"{key}: {json.dumps(value)}" for key, value in fields.items()) + "\n---\n\n"


def metadata(path: Path) -> tuple[dict[str, str], list[str]]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    if not lines or lines[0] != "---":
        return {}, ["missing YAML frontmatter"]
    try:
        end = lines.index("---", 1)
    except ValueError:
        return {}, ["unclosed YAML frontmatter"]
    fields: dict[str, str] = {}
    for line in lines[1:end]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        match = FIELD.match(line)
        if match is None:
            errors.append(f"invalid frontmatter line: {line}")
            continue
        key, value = match.groups()
        if key in fields:
            errors.append(f"duplicate field: {key}")
        fields[key] = value.strip().strip('"\'')
    for required in ("type", "title", "description"):
        if not fields.get(required):
            errors.append(f"missing {required}")
    return fields, errors


def index_text(category: str) -> str:
    directory = ROOT / category
    title = category.title()
    entries = []
    for path in sorted(directory.glob("*.md")):
        if path.name == "index.md":
            continue
        fields, _ = metadata(path)
        name = fields.get("title") or path.stem.replace("_", " ").title()
        description = fields.get("description", "")
        entries.append(f"- [{name}](./{path.name})" + (f" — {description}" if description else ""))
    body = "\n".join(entries) if entries else "No entries yet."
    return frontmatter("Index", title, f"Index of {category} research documents.") + f"# {title}\n\n{body}\n"


def update_indices() -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    for category in CATEGORIES:
        directory = ROOT / category
        directory.mkdir(exist_ok=True)
        (directory / "index.md").write_text(index_text(category), encoding="utf-8")
    links = "\n".join(f"- [{category.title()}](./{category}/index.md)" for category in CATEGORIES)
    (ROOT / "index.md").write_text(
        frontmatter("RootIndex", "Research Knowledge Base", "Index of DisDorktion research documents.")
        + f"# Research Knowledge Base\n\n{links}\n\n- [Change log](./log.md)\n",
        encoding="utf-8",
    )
    log_path = ROOT / "log.md"
    if not log_path.exists():
        log_path.write_text(frontmatter("Log", "Research Change Log", "Chronological research bundle changes.") + "# Research Change Log\n", encoding="utf-8")


def append_log(message: str) -> None:
    ROOT.mkdir(parents=True, exist_ok=True)
    path = ROOT / "log.md"
    if not path.exists():
        path.write_text(frontmatter("Log", "Research Change Log", "Chronological research bundle changes.") + "# Research Change Log\n", encoding="utf-8")
    with path.open("a", encoding="utf-8") as log:
        log.write(f"\n- {now()}: {message.strip()}\n")


def validate() -> list[str]:
    if not ROOT.is_dir():
        return [f"{ROOT}: research bundle does not exist"]
    errors: list[str] = []
    expected = [ROOT / "index.md", ROOT / "log.md"] + [ROOT / category / "index.md" for category in CATEGORIES]
    for path in expected:
        if not path.is_file():
            errors.append(f"{path}: missing required document")
    for path in sorted(ROOT.rglob("*.md")):
        fields, field_errors = metadata(path)
        errors.extend(f"{path}: {error}" for error in field_errors)
        if path == ROOT / "index.md" and fields.get("okf_version") != "0.2":
            errors.append(f"{path}: okf_version must be 0.2")
        text = path.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            target = unquote(target.split("#", 1)[0].strip().split(" ", 1)[0])
            if not target or re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target) or target.startswith("//"):
                continue
            destination = (path.parent / target).resolve()
            if not destination.is_relative_to(ROOT.resolve()) or not destination.exists():
                errors.append(f"{path}: broken link {target}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate", action="store_true", help="check frontmatter and local links")
    parser.add_argument("--update-indices", action="store_true", help="create or refresh category and root indexes")
    parser.add_argument("--log", metavar="MESSAGE", help="append a dated change log entry")
    args = parser.parse_args()
    if not (args.validate or args.update_indices or args.log):
        parser.error("choose --validate, --update-indices, or --log")
    if args.update_indices:
        update_indices()
    if args.log:
        append_log(args.log)
    if args.validate:
        errors = validate()
        for error in errors:
            print(error, file=sys.stderr)
        if errors:
            return 1
        print("OKF bundle valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
