#!/usr/bin/env python3
"""Fetch an arXiv paper and draft an OKF document in docs/okf/papers."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote, urlparse


PAPERS = Path(__file__).resolve().parents[1] / "docs" / "okf" / "papers"
LOCAL_POPPLER = Path(__file__).resolve().parents[1] / ".tools" / "poppler"
ATOM = "http://www.w3.org/2005/Atom"
ID = re.compile(r"(?:\d{4}\.\d{4,5}|[a-z-]+(?:\.[A-Z]{2})?/\d{7})(?:v\d+)?", re.IGNORECASE)


def paper_id(value: str) -> str:
    parsed = urlparse(value)
    if parsed.scheme in ("http", "https") and parsed.netloc.lower() not in ("arxiv.org", "www.arxiv.org", "export.arxiv.org"):
        raise ValueError(f"not an arXiv URL: {value}")
    candidate = parsed.path if parsed.scheme else value
    candidate = re.sub(r"^(?:arxiv:|/(?:abs|pdf)/)", "", candidate, flags=re.IGNORECASE)
    candidate = re.sub(r"\.pdf$", "", candidate, flags=re.IGNORECASE).strip("/")
    if not ID.fullmatch(candidate):
        raise ValueError(f"invalid arXiv ID or URL: {value}")
    return candidate


def get(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "DisDorktionResearch/1.0 (academic paper ingestion)"})
    with urllib.request.urlopen(request, timeout=45) as response:
        return response.read()


def metadata(identifier: str) -> dict[str, object]:
    feed = ET.fromstring(get(f"https://export.arxiv.org/api/query?id_list={quote(identifier, safe='')}"))
    entry = feed.find(f"{{{ATOM}}}entry")
    if entry is None or entry.findtext(f"{{{ATOM}}}title", default="").startswith("Error"):
        raise ValueError(f"arXiv paper not found: {identifier}")
    title = " ".join(entry.findtext(f"{{{ATOM}}}title", default="").split())
    abstract = " ".join(entry.findtext(f"{{{ATOM}}}summary", default="").split())
    authors = [" ".join(node.findtext(f"{{{ATOM}}}name", default="").split()) for node in entry.findall(f"{{{ATOM}}}author")]
    published = entry.findtext(f"{{{ATOM}}}published", default="")[:10]
    categories = [node.attrib.get("term", "") for node in entry.findall(f"{{{ATOM}}}category")]
    return {"title": title, "abstract": abstract, "authors": authors, "published": published, "categories": categories}


def slug(title: str, identifier: str) -> str:
    words = re.findall(r"[a-z0-9]+", title.lower())[:10]
    name = "_".join(words).strip("_") or "paper"
    suffix = re.sub(r"[^a-z0-9]+", "_", identifier.lower()).strip("_")
    return f"{name}_{suffix}"


def extract_pdf(identifier: str) -> str:
    converter = shutil.which("pdftotext")
    if converter is None:
        local_converters = sorted(LOCAL_POPPLER.glob("poppler-*/Library/bin/pdftotext.exe"))
        if local_converters:
            converter = str(local_converters[-1])
    if converter is None:
        raise RuntimeError("pdftotext is required; install Poppler on PATH or under .tools/poppler")
    with tempfile.TemporaryDirectory(prefix="disdorktion_arxiv_") as directory:
        pdf = Path(directory) / "paper.pdf"
        pdf.write_bytes(get(f"https://arxiv.org/pdf/{quote(identifier, safe='/')}"))
        result = subprocess.run([converter, "-layout", "-enc", "UTF-8", str(pdf), "-"], capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        if result.returncode != 0 or not result.stdout.strip():
            raise RuntimeError(f"pdftotext failed: {result.stderr.strip() or 'no text extracted'}")
        return result.stdout.replace("\f", "\n").strip()


def draft(identifier: str, info: dict[str, object], full_text: str) -> str:
    title = str(info["title"])
    abstract = str(info["abstract"])
    authors = list(info["authors"])
    fields = {
        "type": "Paper",
        "title": title,
        "description": abstract[:300],
        "timestamp": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "tags": ["arxiv"] + [str(item).lower().replace(".", "-") for item in info["categories"]],
        "resource": f"https://arxiv.org/abs/{identifier}",
        "authors": authors,
        "arxiv_id": identifier,
        "published": str(info["published"]),
    }
    header = "---\n" + "\n".join(f"{key}: {json.dumps(value, ensure_ascii=False)}" for key, value in fields.items()) + "\n---\n\n"
    citation = f"@misc{{arxiv_{identifier.replace('/', '_').replace('.', '_')},\n  title={{{title}}},\n  author={{{' and '.join(authors)}}},\n  year={{{str(info['published'])[:4]}}},\n  eprint={{{identifier}}},\n  archivePrefix={{arXiv}}\n}}"
    return (
        header + f"# {title}\n\n## Source\n\n- Authors: {', '.join(authors)}\n"
        + f"- Published: {info['published']}\n- arXiv: [View paper](https://arxiv.org/abs/{identifier})\n\n"
        + f"## Abstract\n\n{abstract}\n\n## Analysis Notes\n\n"
        + "Add a verified summary, contributions, methods, results, limitations, and relevance after reading the source text below.\n\n"
        + f"## Citation\n\n```bibtex\n{citation}\n```\n\n## Extracted Full Text\n\n{full_text}\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paper", help="arXiv ID, arXiv URL, or PDF URL")
    parser.add_argument("--write", action="store_true", help="write the draft to docs/okf/papers")
    args = parser.parse_args()
    try:
        identifier = paper_id(args.paper)
        info = metadata(identifier)
        full_text = extract_pdf(identifier)
        content = draft(identifier, info, full_text)
        path = PAPERS / f"{slug(str(info['title']), identifier)}.md"
        if args.write:
            PAPERS.mkdir(parents=True, exist_ok=True)
            with path.open("x", encoding="utf-8") as output:
                output.write(content)
            print(path)
        else:
            sys.stdout.write(content)
    except (ValueError, RuntimeError, FileExistsError, OSError, urllib.error.URLError, ET.ParseError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
