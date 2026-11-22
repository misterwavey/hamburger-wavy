#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy the Markdown guides and example configs into docs/config-editor.html.

The editor is a single offline page, so it carries its own copy of the
documentation for its User guide tab. Run this after changing any docs/*.md or
examples/config/*.txt file; `make test` fails while the copy is out of date.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / 'docs/config-editor.html'
BEGIN, END = '<script type="application/json" id="docs">', '</script><!--docs-end-->'


def bundle():
    docs = {p.stem: p.read_text() for p in sorted((ROOT / 'docs').glob('*.md'))}
    examples = {p.name: p.read_text() for p in sorted((ROOT / 'examples/config').glob('*.txt'))}
    text = json.dumps({'docs': docs, 'examples': examples}, ensure_ascii=False, indent=0, sort_keys=True)
    return text.replace('</', '<\\/')  # never close the script element early


def embedded(page):
    start = page.index(BEGIN) + len(BEGIN)
    return page[:start], page[start:page.index(END, start)], page[page.index(END, start):]


def main():
    head, old, tail = embedded(PAGE.read_text())
    new = '\n' + bundle() + '\n'
    if '--check' in sys.argv:
        if old != new:
            sys.exit('docs/config-editor.html carries an out-of-date copy of the guides: run tools/embed_docs.py')
        return
    PAGE.write_text(head + new + tail)


if __name__ == '__main__':
    main()
