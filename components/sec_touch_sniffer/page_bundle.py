"""Bundles the sniffer page into one gzipped HTML file for flash.

The page (web/sniffer_page.html) imports plain ES modules from web/ so `node --test` can test them. The device
serves a single file, so the build inlines those modules into the page's module script: dependencies first, each
once, with their `import` lines and `export` keywords removed. Only the forms the page uses are supported, and
anything else fails the build rather than shipping a page that breaks in the browser:

- `import { a, b } from "./name.mjs";` (may span lines; no `as` renames)
- `export function`, `export async function`, `export const`, `export let`, `export class`
"""

import gzip
from pathlib import Path
import re

WEB_DIR = Path(__file__).parent / "web"
PAGE_FILE = "sniffer_page.html"

_SCRIPT = re.compile(r'(<script type="module">)(.*?)(</script>)', re.S)
_IMPORT = re.compile(r'^import\s*\{([^}]*)\}\s*from\s*"\./([\w.-]+\.mjs)";[ \t]*\n?', re.M)
_EXPORT = re.compile(r"^export (?=(?:async )?function |const |let |class )", re.M)
_LEFTOVER = re.compile(r"^\s*(?:import|export)\b", re.M)
_TOP_LEVEL_NAME = re.compile(r"^(?:async )?(?:function\*?|const|let|class) (\w+)", re.M)


def _module_body(source: str, name: str, resolve) -> str:
    """The source with its imports resolved through `resolve` and removed, and `export` dropped."""
    for names, dependency in _IMPORT.findall(source):
        if " as " in names:
            raise ValueError(f"{name}: `as` renames are not supported by the page bundle")
        resolve(dependency)
    body = _EXPORT.sub("", _IMPORT.sub("", source))
    leftover = _LEFTOVER.search(body)
    if leftover is not None:
        line = body[leftover.start() :].splitlines()[0]
        raise ValueError(f"{name}: unsupported import or export form for the page bundle: {line.strip()}")
    return body


def bundle_page(web_dir: Path = WEB_DIR) -> str:
    """The page with every module it imports inlined into its module script."""
    html = (web_dir / PAGE_FILE).read_text(encoding="utf-8")
    scripts = _SCRIPT.findall(html)
    if len(scripts) != 1:
        raise ValueError(f"{PAGE_FILE} must have exactly one <script type=\"module\">, found {len(scripts)}")

    bodies = []
    done = set()
    in_progress = set()

    def resolve(module: str):
        if module in done:
            return
        if module in in_progress:
            raise ValueError(f"{module}: import cycle in the page modules")
        in_progress.add(module)
        source = (web_dir / module).read_text(encoding="utf-8")
        body = _module_body(source, module, resolve)
        in_progress.discard(module)
        done.add(module)
        bodies.append(f"// ── {module} ──\n{body.strip()}\n")

    page_script = _module_body(scripts[0][1], PAGE_FILE, resolve)
    script = "\n".join([*bodies, f"// ── {PAGE_FILE} ──\n{page_script.strip()}\n"])

    names = _TOP_LEVEL_NAME.findall(script)
    duplicates = sorted({n for n in names if names.count(n) > 1})
    if duplicates:
        raise ValueError(f"page modules declare the same top-level names: {', '.join(duplicates)}")

    return _SCRIPT.sub(lambda m: f"{m.group(1)}\n{script}{m.group(3)}", html, count=1)


def gzipped_page(web_dir: Path = WEB_DIR) -> bytes:
    """The bundled page, gzipped. mtime=0 keeps the bytes the same for the same page."""
    return gzip.compress(bundle_page(web_dir).encode("utf-8"), mtime=0)
