#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The manual's pages (app/docs/*.md) rendered as the site renders them: every relative link reaches a page and an
anchor that exist, every image is in app/docs/img and declares its size, and no page has an id twice. Exit 1 with one line per fault."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / "app" / "docs"


def html_name(md):
    return "index.html" if md.name == "MANUAL.md" else md.stem + ".html"


def main():
    faults, ids, pages = [], {}, {}
    with tempfile.TemporaryDirectory() as tmp:
        for md in sorted(DOCS.glob("*.md")):
            out = Path(tmp) / html_name(md)
            subprocess.run([sys.executable, str(ROOT / "base" / "tools" / "manual_html.py"), str(md), str(out),
                            "--name", "QUARTZ"], check=True, capture_output=True)
            page = out.read_text(encoding="utf-8")
            found = re.findall(r'\sid="([^"]+)"', page)
            for i in sorted({i for i in found if found.count(i) > 1}):
                faults.append(f"app/docs/{md.name}: duplicate id {i}")
            ids[out.name], pages[md.name] = set(found), page
    for name, page in pages.items():
        here = html_name(DOCS / name)
        for href in re.findall(r'<a [^>]*href="([^"]+)"', page):
            if re.match(r"[a-z]+:", href) or href.startswith("../") or href == "./":
                continue                        # outside the manual: other sites, the site's own pages
            target, _, anchor = href.partition("#")
            target = target or here
            if target not in ids or (anchor and anchor not in ids[target]):
                faults.append(f"app/docs/{name}: broken link {href}")
        for tag in re.findall(r'<img [^>]*>', page):
            src = re.search(r'src="([^"]+)"', tag).group(1)
            if not (DOCS / src).is_file():
                faults.append(f"app/docs/{name}: missing image {src}")
            if not re.search(r'\swidth="\d+"', tag) or not re.search(r'\sheight="\d+"', tag):
                # without them a lazy image has no height until it loads: the page shifts and a link to an anchor
                # lands in the wrong place
                faults.append(f"app/docs/{name}: image without width and height {src}")
    for f in faults:
        print(f)
    if faults:
        sys.exit(1)
    print("doc links ok")


if __name__ == "__main__":
    main()
