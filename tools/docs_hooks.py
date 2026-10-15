"""MkDocs hook: keep the navigation in reading order.

mkdocs-static-i18n (docs_structure: folder) sorts the top-level navigation by title, and the
automatic navigation sorts chapters by file name. tools/build_site.py writes the intended page
order of each language to site_src/.nav_order.json; this hook re-sorts the navigation with it,
after the i18n plugin has run.
"""

import json
from pathlib import Path

from mkdocs.plugins import event_priority
from mkdocs.structure.nav import Section

_ORDER = {}


def on_config(config):
    path = Path(config["docs_dir"]) / ".nav_order.json"
    _ORDER.clear()
    if path.exists():
        _ORDER.update(json.loads(path.read_text(encoding="utf-8")))
    return config


def _rank(item, order):
    if isinstance(item, Section):
        ranks = [_rank(c, order) for c in item.children]
        return min(ranks) if ranks else len(order)
    path = item.file.src_uri.split("/", 1)[-1] if getattr(item, "file", None) else ""
    return order.index(path) if path in order else len(order)


def _sort(items, order):
    items.sort(key=lambda i: _rank(i, order))
    for item in items:
        if isinstance(item, Section):
            _sort(item.children, order)


@event_priority(-200)
def on_nav(nav, config, files):
    lang = None
    for item in nav.pages[:1]:
        lang = item.file.src_uri.split("/", 1)[0]
    order = _ORDER.get(lang) or _ORDER.get(config.get("theme", {}).get("language", "en"), [])
    _sort(nav.items, order)
    return nav
