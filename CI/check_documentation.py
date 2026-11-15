#!/usr/bin/env python3
"""Check local links in OpenFallout's developer documentation entrypoints."""

from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_ENTRYPOINTS = (
    Path("CONTRIBUTING.md"),
    Path("docs/openfallout/architecture.md"),
    Path("docs/openfallout/shell-interface.md"),
)
RST_ENTRYPOINTS = (
    Path("docs/source/openfallout.rst"),
    Path("docs/source/index.rst"),
)
REPO_BLOB_PREFIX = "https://github.com/OpenBethesda/OpenFallout/blob/master/"
MARKDOWN_LINK = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
# Any owner: a link to the repository under its former owner or to a fork is reported as non-canonical
REPO_BLOB_LINK = re.compile(r"<((?:https://github\.com/[^/>\s]+/OpenFallout/blob/)[^>]+)>_?")
DOC_ROLE = re.compile(r":doc:`(?:[^`<>]*<)?([^`<>]+)>?`")
TOCTREE = re.compile(r"^(?P<indent>\s*)\.\. toctree::\s*$")

# The interface another program (the shell) relies on: the page lists it, the engine test asks the real option parser and
# Lua menu package for each name, and the menu scripts' engine handlers are declared in the header below. The page and the
# test must name the same options and functions, so that "not changed without notice" is held by a machine.
SHELL_INTERFACE_PAGE = Path("docs/openfallout/shell-interface.md")
SHELL_INTERFACE_TEST = Path("apps/openfallout_tests/shellinterface.cpp")
SHELL_INTERFACE_HANDLERS = Path("apps/openfallout/mwlua/menuscripts.hpp")
SHELL_SECTIONS = {
    "## Command-line options": "options",
    "## Lua: the menu package": "functions",
    "## Lua: engine handlers of menu scripts": "handlers",
}
TABLE_FIRST_CELL = re.compile(r"^\|\s*`([^`]+)`")


def local_target(source: Path, target: str, root: Path) -> Path | None:
    """Resolve a relative link target, ignoring URLs and in-page anchors."""
    target = target.strip()
    if not target or target.startswith("#"):
        return None
    parsed = urlsplit(target)
    if parsed.scheme or parsed.netloc:
        return None
    path = unquote(parsed.path)
    if not path:
        return None
    return (root / source.parent / path).resolve()


def check_markdown_links(text: str, source: Path, root: Path) -> list[str]:
    errors = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for match in MARKDOWN_LINK.finditer(line):
            target = match.group(1)
            resolved = local_target(source, target, root)
            if resolved is not None and not resolved.exists():
                errors.append(f"{source}:{line_number}: missing link target {target}")
    return errors


def check_repo_blob_links(text: str, source: Path, root: Path) -> list[str]:
    errors = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for match in REPO_BLOB_LINK.finditer(line):
            url = match.group(1)
            if not url.startswith(REPO_BLOB_PREFIX):
                errors.append(f"{source}:{line_number}: non-canonical repository link {url}")
                continue
            target = unquote(url[len(REPO_BLOB_PREFIX):].split("#", 1)[0])
            if not target or not (root / target).exists():
                errors.append(f"{source}:{line_number}: missing repository link target {target}")
    return errors


def check_sphinx_targets(text: str, source: Path, root: Path) -> list[str]:
    errors = []
    source_file = root / source

    def check_doc(target: str, line_number: int) -> None:
        target = target.strip().split("#", 1)[0]
        target_path = Path(target)
        if target_path.suffix != ".rst":
            target_path = target_path.with_suffix(".rst")
        resolved = (source_file.parent / target_path).resolve()
        if not resolved.is_file():
            errors.append(f"{source}:{line_number}: missing Sphinx document {target}")

    lines = text.splitlines()
    for line_number, line in enumerate(lines, start=1):
        for match in DOC_ROLE.finditer(line):
            check_doc(match.group(1), line_number)

        directive = TOCTREE.match(line)
        if not directive:
            continue
        base_indent = len(directive.group("indent"))
        for offset, candidate in enumerate(lines[line_number:], start=line_number + 1):
            if candidate.strip() and len(candidate) - len(candidate.lstrip()) <= base_indent:
                break
            entry = candidate.strip()
            if not entry or entry.startswith(":"):
                continue
            check_doc(entry, offset)
    return errors


def shell_interface_names(page: str) -> dict[str, list[str]]:
    """The names in the first column of the tables of the page's three sections: options without their dashes and
    value, functions and handlers without their arguments."""
    names: dict[str, list[str]] = {kind: [] for kind in SHELL_SECTIONS.values()}
    kind = None
    for line in page.splitlines():
        if line.startswith("## "):
            kind = SHELL_SECTIONS.get(line.strip())
            continue
        match = TABLE_FIRST_CELL.match(line)
        if kind is None or match is None:
            continue
        cell = match.group(1)
        name = cell.split()[0].removeprefix("--") if kind == "options" else cell.split("(", 1)[0]
        names[kind].append(name.strip())
    return names


def quoted_list(source: str, array: str) -> list[str]:
    """The string literals of `array[] = { ... };` in a C++ source."""
    match = re.search(re.escape(array) + r"\[\]\s*=\s*\{(.*?)\};", source, re.DOTALL)
    return re.findall(r'"([^"]+)"', match.group(1)) if match else []


def check_shell_interface(page: str, test: str, handlers: str) -> list[str]:
    errors = []
    names = shell_interface_names(page)
    source = str(SHELL_INTERFACE_PAGE)
    checked = {"options": quoted_list(test, "keptOptions"), "functions": quoted_list(test, "keptMenuFunctions")}
    if not names["options"] or not checked["options"]:
        errors.append(f"{source}: no command-line options found on the page or in {SHELL_INTERFACE_TEST}")
    for name in names["options"]:
        if name not in checked["options"]:
            errors.append(f"{source}: option --{name} is listed but {SHELL_INTERFACE_TEST} does not check it")
    for name in checked["options"]:
        if name not in names["options"]:
            errors.append(f"{SHELL_INTERFACE_TEST}: option --{name} is checked but {source} does not list it")
    for name in names["functions"]:
        if name not in checked["functions"] and f'"{name}"' not in test:
            errors.append(f"{source}: menu.{name} is listed but {SHELL_INTERFACE_TEST} does not check it")
    for name in checked["functions"]:
        if name not in names["functions"]:
            errors.append(f"{SHELL_INTERFACE_TEST}: menu.{name} is checked but {source} does not list it")
    for name in names["handlers"]:
        if f'{{ "{name}" }}' not in handlers:
            errors.append(f"{source}: handler {name} is listed but is not an engine handler in {SHELL_INTERFACE_HANDLERS}")
    return errors


def validate(root: Path = ROOT) -> list[str]:
    errors = []
    for source in MARKDOWN_ENTRYPOINTS:
        path = root / source
        if not path.is_file():
            errors.append(f"{source}: missing documentation entrypoint")
            continue
        errors.extend(check_markdown_links(path.read_text(encoding="utf-8"), source, root))

    for source in RST_ENTRYPOINTS:
        path = root / source
        if not path.is_file():
            errors.append(f"{source}: missing documentation entrypoint")
            continue
        text = path.read_text(encoding="utf-8")
        errors.extend(check_repo_blob_links(text, source, root))
        errors.extend(check_sphinx_targets(text, source, root))

    shell_files = (SHELL_INTERFACE_PAGE, SHELL_INTERFACE_TEST, SHELL_INTERFACE_HANDLERS)
    missing = [str(path) for path in shell_files if not (root / path).is_file()]
    if missing:
        errors.extend(f"{path}: missing file of the shell interface" for path in missing)
    else:
        errors.extend(check_shell_interface(*((root / path).read_text(encoding="utf-8") for path in shell_files)))
    return errors


def main() -> int:
    errors = validate()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Documentation entrypoint links and the shell interface list are valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
