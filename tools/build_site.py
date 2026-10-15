#!/usr/bin/env python3
"""Generate the MkDocs source tree (site_src/) for the Modbus Control documentation site.

Run from anywhere:  python tools/build_site.py   then   mkdocs build

Sources (kept untouched, the installer ships them as they are):
    docs/QUICK_START.md, docs/USER_GUIDE.md,
    docs/fr/GUIDE_DEMARRAGE_RAPIDE.md, docs/fr/GUIDE_UTILISATEUR.md

Generated (site_src/ is wiped first and is git-ignored):
    en/index.md, fr/index.md                  Quick Start
    en/guide/*.md, fr/guide/*.md              the user guide, one page per "## " chapter
    en/download.md, fr/telechargement.md      download page
    en/changelog.md, fr/changelog.md          release notes from the GitHub releases API
    .nav_order.json                           page order, read by tools/docs_hooks.py (the
                                              i18n plugin sorts the navigation by title, so
                                              the hook puts it back in reading order)

Links are rewritten from the GitHub-style relative links of the sources to the generated
pages. A link to an anchor that no heading defines makes the script exit with status 1.
Python 3.12, standard library only.
"""

from __future__ import annotations

import json
import os
import posixpath
import re
import shutil
import sys
import unicodedata
import urllib.request
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
OUT = ROOT / "site_src"
REPO_URL = "https://github.com/francisboudreau7/modbus-control"
BLOB_URL = REPO_URL + "/blob/main/"
RELEASES_PAGE = REPO_URL + "/releases"
API_URL = "https://api.github.com/repos/francisboudreau7/modbus-control/releases"

# source file (relative to the repository root) -> (language, kind)
SOURCES = {
    "docs/QUICK_START.md": ("en", "quick"),
    "docs/USER_GUIDE.md": ("en", "guide"),
    "docs/fr/GUIDE_DEMARRAGE_RAPIDE.md": ("fr", "quick"),
    "docs/fr/GUIDE_UTILISATEUR.md": ("fr", "guide"),
}

# Language-switch text that the theme's language selector replaces. Each pattern must
# match exactly once in its source, otherwise the build fails (the source changed).
SWITCH_PATTERNS = {
    "docs/QUICK_START.md": [r"^Version française : \[[^\]]*\]\([^)]*\)\.?[ \t]*\n",
                            r"^Read online: \[[^\]]*\]\([^)]*\)[ \t]*\n\n"],
    "docs/USER_GUIDE.md": [r"[ \t]*En français :\s*\[[^\]]*\]\([^)]*\)",
                           r"^Read online: \[[^\]]*\]\([^)]*\)[ \t]*\n\n"],
    "docs/fr/GUIDE_DEMARRAGE_RAPIDE.md": [r"English version: \[[^\]]*\]\([^)]*\)\s*·\s*",
                                          r"^Lire en ligne : \[[^\]]*\]\([^)]*\)[ \t]*\n\n"],
    "docs/fr/GUIDE_UTILISATEUR.md": [r"English version: \[[^\]]*\]\([^)]*\)\s*·\s*",
                                     r"^Lire en ligne : \[[^\]]*\]\([^)]*\)[ \t]*\n\n"],
}

errors: list[str] = []


def fail(msg: str) -> None:
    errors.append(msg)


# --------------------------------------------------------------------------- helpers

def fold_ascii(text: str) -> str:
    return "".join(
        c for c in unicodedata.normalize("NFKD", text) if not unicodedata.combining(c)
    )


def page_slug(title: str) -> str:
    """ASCII-folded, lower-case, hyphenated file name for a chapter title."""
    s = fold_ascii(title).lower().replace("'", "").replace("’", "")
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s


def plain_heading(text: str) -> str:
    """Heading text as the Markdown renderer sees it (no inline markup)."""
    text = re.sub(r"!?\[([^\]]*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"[`*]", "", text)
    return text.strip()


def anchor_of(heading_text: str) -> str:
    """GitHub-style anchor; identical to pymdownx.slugs.slugify(case='lower')."""
    s = unicodedata.normalize("NFC", plain_heading(heading_text)).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def split_fences(text: str):
    """Yield (is_code, chunk) over lines, treating fenced code blocks as code."""
    fence = None
    buf: list[str] = []
    code = False
    for line in text.splitlines(keepends=True):
        m = re.match(r"^\s*(```+|~~~+)", line)
        if fence is None and m:
            if buf:
                yield code, "".join(buf)
            buf, code, fence = [line], True, m.group(1)[0] * 3
            continue
        if fence is not None:
            buf.append(line)
            if line.lstrip().startswith(fence):
                yield True, "".join(buf)
                buf, code, fence = [], False, None
            continue
        buf.append(line)
    if buf:
        yield code, "".join(buf)


def headings(text: str):
    """Yield (level, title) for each ATX heading outside fenced code blocks."""
    for is_code, chunk in split_fences(text):
        if is_code:
            continue
        for line in chunk.splitlines():
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if m:
                yield len(m.group(1)), m.group(2)


def shift_headings(text: str, delta: int) -> str:
    out = []
    for is_code, chunk in split_fences(text):
        if is_code:
            out.append(chunk)
            continue
        lines = []
        for line in chunk.splitlines(keepends=True):
            m = re.match(r"^(#{1,6})(\s+.*)$", line, re.S)
            if m:
                level = min(6, max(1, len(m.group(1)) + delta))
                line = "#" * level + m.group(2)
            lines.append(line)
        out.append("".join(lines))
    return "".join(out)


def front_matter(title: str) -> str:
    return '---\ntitle: "%s"\n---\n\n' % title.replace('"', '\\"')


# --------------------------------------------------------------------------- guide model

class Doc:
    """One source document, split into output pages."""

    def __init__(self, rel: str, lang: str, kind: str):
        self.rel = rel
        self.lang = lang
        self.kind = kind
        text = (ROOT / rel).read_text(encoding="utf-8").replace("\r\n", "\n")
        for pat in SWITCH_PATTERNS[rel]:
            text, n = re.subn(pat, "", text, count=1, flags=re.M)
            if n != 1:
                fail(f"{rel}: language-switch pattern not found: {pat}")
        self.text = text
        # output page (path relative to site_src/<lang>/) -> markdown
        self.pages: dict[str, str] = {}
        # original anchor -> (output page, new anchor)
        self.anchors: dict[str, tuple[str, str]] = {}
        self.doc_counts: dict[str, int] = {}
        self.order: list[tuple[str, str]] = []  # (page, title)
        if kind == "quick":
            self._single()
        else:
            self._split()

    def _register(self, text: str, page: str, shift: int) -> None:
        """Map each heading's GitHub anchor to (page, anchor generated by Python-Markdown).

        GitHub numbers repeated headings of a document base, base-1, ...; Python-Markdown
        numbers repeats within a page base, base_1, ... (the toc extension).
        """
        page_counts: dict[str, int] = {}
        for _, title in headings(text):
            base = anchor_of(title)
            n_page = page_counts.get(base, 0)
            page_counts[base] = n_page + 1
            new = base if n_page == 0 else f"{base}_{n_page}"
            n_doc = self.doc_counts.get(base, 0)
            self.doc_counts[base] = n_doc + 1
            gh = base if n_doc == 0 else f"{base}-{n_doc}"
            self.anchors[gh] = (page, new)

    def _single(self) -> None:
        page = "index.md"
        self._register(self.text, page, 0)
        self.pages[page] = self.text

    def _split(self) -> None:
        # chunks of the document at each "## " heading outside fences
        chunks: list[list[str]] = [[]]
        for is_code, chunk in split_fences(self.text):
            if is_code:
                chunks[-1].append(chunk)
                continue
            for line in chunk.splitlines(keepends=True):
                if re.match(r"^## ", line):
                    chunks.append([])
                chunks[-1].append(line)
        intro = "".join(chunks[0])
        self._register(intro, "guide/index.md", 0)
        self.pages["guide/index.md"] = intro
        used: set[str] = {"index"}
        for parts in chunks[1:]:
            body = "".join(parts)
            title = re.match(r"^## (.*)", body).group(1).strip()
            slug = page_slug(title)
            if slug in used:
                fail(f"{self.rel}: two chapters map to the page name '{slug}'")
            used.add(slug)
            page = f"guide/{slug}.md"
            self._register(body, page, 1)
            self.pages[page] = shift_headings(body, -1)
            self.order.append((page, plain_heading(title)))


# --------------------------------------------------------------------------- link rewriting

INLINE_CODE = re.compile(r"(`+)(?:(?!\1).)+?\1", re.S)
LINK = re.compile(r"(?<!!)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)((?:\s+\"[^\"]*\")?)\)")


def rewrite_links(doc: Doc, docs_by_rel: dict[str, Doc]) -> None:
    src_dir = PurePosixPath(doc.rel).parent

    def target_for(link: str, page: str) -> str | None:
        """New link target for `link` found on output page `page` (of doc.lang)."""
        if re.match(r"^[a-z][a-z0-9+.-]*:", link, re.I):
            return None  # external URL, mailto:, ...
        path, _, frag = link.partition("#")
        if path:
            repo_path = PurePosixPath(posixpath.normpath(str(src_dir / path))).as_posix()
        else:
            repo_path = doc.rel
        target = docs_by_rel.get(repo_path)
        if target is None:
            if not path:
                return None
            if not (ROOT / repo_path).exists():
                fail(f"{doc.rel}: link target does not exist: {link}")
                return None
            return BLOB_URL + repo_path + (("#" + frag) if frag else "")
        # a link to another generated document
        if frag:
            hit = target.anchors.get(frag)
            if hit is None:
                fail(f"{doc.rel}: link '{link}' points to an anchor no heading defines")
                return None
            new_page, new_anchor = hit
        else:
            new_page = "index.md" if target.kind == "quick" else "guide/index.md"
            new_anchor = ""
        from_page = PurePosixPath(doc.lang) / page
        to_page = PurePosixPath(target.lang) / new_page
        rel = os.path.relpath(to_page, from_page.parent).replace("\\", "/")
        return rel + (("#" + new_anchor) if new_anchor else "")

    for page, text in list(doc.pages.items()):
        out = []
        for is_code, chunk in split_fences(text):
            if is_code:
                out.append(chunk)
                continue
            masked: list[str] = []

            def mask(m):
                masked.append(m.group(0))
                return f"\x00{len(masked) - 1}\x00"

            chunk = INLINE_CODE.sub(mask, chunk)

            def sub(m):
                new = target_for(m.group(2), page)
                if new is None:
                    return m.group(0)
                return f"[{m.group(1)}]({new}{m.group(3)})"

            chunk = LINK.sub(sub, chunk)
            chunk = re.sub(r"\x00(\d+)\x00", lambda m: masked[int(m.group(1))], chunk)
            out.append(chunk)
        doc.pages[page] = "".join(out)


# --------------------------------------------------------------------------- extra pages

DOWNLOAD_EN = """\
---
title: "Download"
---

# Download

Get the latest installer from the
[GitHub releases page]({releases}/latest). The installer is a single file named
`ModbusControl-<version>-setup.exe`.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- A Modbus TCP device on your network, such as a controller (for example an ABB ControlMaster
  CM50). To try the application without hardware, use the test server that is installed with it.

## What the installer contains

- The Modbus Control application.
- The Modbus test server (`modbus_test_server`), to try the application without a device.
- The Quick Start and the User Guide, in English and in Quebec French.
- Example projects (`.mbproj` files) to open and try.

## Installing and updating

Run the installer and follow the steps. It installs over an earlier version and keeps your
settings, so updating is the same as installing.

## "Unknown publisher" warning

The installer is not code-signed, so Windows SmartScreen may show **Windows protected your PC**
with an unknown publisher. This is expected. Click **More info**, then **Run anyway**.

## Source code

Modbus Control is open source. The source code, the issue tracker and every release are on
[GitHub]({repo}).
"""

DOWNLOAD_FR = """\
---
title: "Téléchargement"
---

# Téléchargement

Téléchargez le dernier programme d'installation depuis la
[page des versions sur GitHub]({releases}/latest). Il s'agit d'un seul fichier nommé
`ModbusControl-<version>-setup.exe`.

## Configuration requise

- Windows 10 ou Windows 11, 64 bits.
- Un appareil Modbus TCP sur votre réseau, par exemple un régulateur (comme l'ABB ControlMaster
  CM50). Pour essayer le logiciel sans matériel, utilisez le serveur de test installé avec lui.

## Contenu du programme d'installation

- Le logiciel Modbus Control.
- Le serveur de test Modbus (`modbus_test_server`), pour essayer le logiciel sans appareil.
- Le Guide de démarrage rapide et le Guide de l'utilisateur, en anglais et en français québécois.
- Des projets d'exemple (fichiers `.mbproj`) à ouvrir et à essayer.

## Installation et mise à jour

Lancez le programme d'installation et suivez les étapes. Il s'installe par-dessus une version
précédente et conserve vos réglages : mettre à jour revient donc à installer.

## Avertissement « éditeur inconnu »

Le programme d'installation n'est pas signé numériquement : Windows SmartScreen peut afficher
**Windows a protégé votre ordinateur** avec un éditeur inconnu. C'est normal. Cliquez sur
**Informations complémentaires**, puis sur **Exécuter quand même**.

## Code source

Modbus Control est un logiciel libre. Le code source, le suivi des problèmes et chaque version
sont sur [GitHub]({repo}).
"""


def fetch_releases():
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "modbus-control-docs"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    releases = []
    for page in range(1, 6):
        req = urllib.request.Request(f"{API_URL}?per_page=100&page={page}", headers=headers)
        with urllib.request.urlopen(req, timeout=20) as resp:
            batch = json.load(resp)
        releases.extend(batch)
        if len(batch) < 100:
            break
    return [r for r in releases if not r.get("draft")]


def changelog_page(lang: str) -> str:
    fr = lang == "fr"
    title = "Notes de version" if fr else "Release notes"
    head = f'---\ntitle: "{title}"\n---\n\n# {title}\n\n'
    if fr:
        intro = "Les notes de version sont rédigées en anglais.\n\n"
        fallback = (
            f"Les notes de version sont sur la [page des versions GitHub]({RELEASES_PAGE}).\n"
        )
    else:
        intro = ""
        fallback = f"The release notes are on the [GitHub releases page]({RELEASES_PAGE}).\n"
    try:
        releases = fetch_releases()
        if not releases:
            raise RuntimeError("no releases returned")
    except Exception as exc:  # noqa: BLE001 - the build must not fail because of this
        print(f"warning: could not fetch the releases ({exc}); writing a fallback page")
        return head + intro + fallback
    releases.sort(key=lambda r: r.get("published_at") or r.get("created_at") or "", reverse=True)
    parts = [head, intro]
    for r in releases:
        name = (r.get("name") or r.get("tag_name") or "").strip()
        tag = r.get("tag_name") or ""
        date = (r.get("published_at") or r.get("created_at") or "")[:10]
        label = name if name else tag
        parts.append(f"## {label}\n\n")
        meta = f"{date}" if date else ""
        if r.get("html_url"):
            link = f"[{'Voir sur GitHub' if fr else 'View on GitHub'}]({r['html_url']})"
            meta = f"{meta} · {link}" if meta else link
        if meta:
            parts.append(f"*{meta}*\n\n")
        body = (r.get("body") or "").replace("\r\n", "\n").strip()
        if body:
            levels = [lvl for lvl, _ in headings(body)]
            delta = (3 - min(levels)) if levels else 0  # nest under the release's "## "
            parts.append(shift_headings(body, delta) + "\n\n")
    return "".join(parts)


# --------------------------------------------------------------------------- main

def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    docs = {rel: Doc(rel, lang, kind) for rel, (lang, kind) in SOURCES.items()}
    for d in docs.values():
        rewrite_links(d, docs)

    if errors:
        print("build_site.py: errors found:", file=sys.stderr)
        for e in errors:
            print("  - " + e, file=sys.stderr)
        return 1

    if OUT.exists():
        shutil.rmtree(OUT)

    nav_order: dict[str, list[str]] = {}
    for d in docs.values():
        lang = d.lang
        for page, text in d.pages.items():
            if page == "index.md" and d.kind == "quick":
                title = "Démarrage rapide" if lang == "fr" else "Quick Start"
                text = front_matter(title) + text
            elif page == "guide/index.md":
                text = front_matter("Guide de l'utilisateur" if lang == "fr" else "User Guide") + text
            write(OUT / lang / page, text)
        if d.kind == "guide":
            # chapter list on the guide's front page
            idx = OUT / lang / "guide" / "index.md"
            head = "Chapitres" if lang == "fr" else "Chapters"
            items = "".join(f"- [{t}]({p.split('/')[-1]})\n" for p, t in d.order)
            idx.write_text(
                idx.read_text(encoding="utf-8").rstrip() + f"\n\n## {head}\n\n{items}",
                encoding="utf-8",
                newline="\n",
            )
            nav_order.setdefault(lang, [])
            nav_order[lang] += ["guide/index.md"] + [p for p, _ in d.order]
        else:
            nav_order.setdefault(lang, [])
            nav_order[lang].insert(0, "index.md")

    write(OUT / "en" / "download.md", DOWNLOAD_EN.format(releases=RELEASES_PAGE, repo=REPO_URL))
    write(OUT / "fr" / "telechargement.md", DOWNLOAD_FR.format(releases=RELEASES_PAGE, repo=REPO_URL))
    write(OUT / "en" / "changelog.md", changelog_page("en"))
    write(OUT / "fr" / "changelog.md", changelog_page("fr"))
    nav_order["en"].append("download.md")
    nav_order["fr"].append("telechargement.md")
    for lang in nav_order:
        nav_order[lang].append("changelog.md")
    write(OUT / ".nav_order.json", json.dumps(nav_order, indent=1))

    n = sum(1 for _ in OUT.rglob("*.md"))
    print(f"build_site.py: wrote {n} pages to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
