#!/usr/bin/env python3
"""Check generated LDoc links, anchors and offline assets using only the stdlib."""
import argparse
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__(convert_charrefs=True)
        self.anchors = set()
        self.links = []
        self.feed(path.read_text(encoding="utf-8"))

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        for key in ("id", "name"):
            if key in attrs:
                self.anchors.add(attrs[key])
        if "href" in attrs:
            self.links.append((attrs["href"], tag == "link"))
        if "src" in attrs:
            self.links.append((attrs["src"], True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manual", type=Path)
    args = parser.parse_args()
    root = args.manual.resolve()
    api = root / "lua-api"
    required = [api / "index.html", root / "index.html"]
    required += [api / "modules" / f"{name}.html" for name in ("snapshot", "generator", "curve", "project", "chart")]
    required += [api / "examples" / f"{name}.lua.html" for name in ("generator", "curve", "factory", "project", "easy.chart")]
    required += [api / "sources" / f"{name}.lua" for name in ("generator", "curve", "factory", "project", "easy.chart")]
    required += [api / "licenses" / name for name in
                 ("LDoc.txt", "Penlight.txt", "LuaFileSystem.txt", "Lua-header-with-license.txt")]
    failures = [f"missing {path}" for path in required if not path.is_file()]
    pages = {path.resolve(): Page(path) for path in api.rglob("*.html")}
    manual = root / "index.html"
    if manual.is_file() and not any(link == "lua-api/index.html" for link, _ in Page(manual).links):
        failures.append(f"{manual}: no link to the Lua API reference")
    for path, page in pages.items():
        for link, asset in page.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                if asset:
                    failures.append(f"{path}: remote asset {link}")
                continue
            target = (path.parent / unquote(url.path)).resolve() if url.path else path
            if not target.is_file():
                failures.append(f"{path}: missing local link {link}")
            elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].anchors:
                failures.append(f"{path}: missing anchor {link}")
    if failures:
        parser.exit(1, "\n".join(failures) + "\n")
    print(f"Lua API: {len(pages)} HTML pages, local links/anchors and offline assets passed")


if __name__ == "__main__":
    main()
