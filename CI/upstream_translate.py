#!/usr/bin/env python3
"""Turn a path or a file of upstream OpenMW into the name and text it has in OpenFallout.

OpenFallout renamed OpenMW's source on 4 October 2026 (docs/openfallout/gap-audit-and-roadmap.md, "Rename to
OpenFallout"): `apps/openmw` became `apps/openfallout`, the `MW*` namespaces became `OF*`, `OPENMW_*` became
`OPENFALLOUT_*`, and so on. A change made upstream after that day names the old paths and the old identifiers, so it
cannot be merged as it is. CI/upstream_sync.py translates both sides of an upstream change (the old file and the new
file) with the two functions below and then does a three-way merge of the translated files, so that a line the
change did not touch is not a false conflict and a line the change adds already has our names.

The rename was done by hand and by script, and it was narrow on purpose: it left the copyright and history files, the
Morrowind-era manuals, the widget layouts and artwork, the GLSL names and the file extensions alone (stage D of the
plan). So the rules below are not "replace OpenMW by OpenFallout everywhere" but a list of what the rename touched,
with the exceptions it made. They were fitted to the real rename and are checked with

    python3 CI/upstream_translate.py --measure f90f239d91 5c09edbb9f [--list]

which translates every file of the last upstream commit in our history (f90f239d91) and compares the result with the
tree right after the rename (5c09edbb9f). The files that still differ are lines that were reformatted after the
rename made them too long, text that people wrote on that day (the README, the census tool, the .desktop files),
and a few places where the rename itself is not consistent (the same word kept in one file and changed in another)
that no rule can predict.

Both sides of a change go through the same function, so a rule that is wrong in the same way on both sides only
matters on the lines the change itself adds.
"""

import re
import subprocess
import sys


# ---- Paths ----------------------------------------------------------------------------------------------------------

# (pattern, replacement) for paths. The first match wins. Only the names the rename really moved are listed:
# many other paths still say "openmw" or "omw" on purpose (the widget layouts files/data/mygui/openmw_*.layout, the
# artwork openmw.png and openmw.ico, the Morrowind-era manuals docs/source/manuals/*openmw*, scripts/omw, the
# *.omwscripts and *.omwfx files, which stage D of the plan leaves for later), so a general "openmw" rule on paths
# would be wrong. A new file inside one of the renamed places gets the new name too.
PATH_RULES = tuple((re.compile(pattern), replacement) for pattern, replacement in (
    (r"^apps/openmw_tests/", "apps/openfallout_tests/"),
    (r"^apps/openmw/", "apps/openfallout/"),
    (r"^files/lua_api/openmw/", "files/lua_api/openfallout/"),
    # Lua modules `openmw_aux.*`, in files/data and in files/data-mw.
    (r"^(files/data(?:-mw)?)/openmw_aux/", r"\1/openfallout_aux/"),
    # The generated reference pages of the Lua API, one per module (openmw_core.rst, openmw_aux_ui.rst, ...).
    (r"^(docs/source/reference/lua-scripting/)openmw_", r"\1openfallout_"),
    # The l10n contexts of the engine's own scripts: OMWCamera, OMWEngine and so on.
    (r"^(files/(?:data|data-mw)/l10n/)OMW", r"\1OF"),
    # Configuration files, wherever a test or a package keeps one.
    (r"^(.*/)?openmw(-cs)?\.cfg(\.local)?$", r"\1openfallout\2.cfg\3"),
    (r"^files/openmw\.appdata\.xml$", "files/openfallout.appdata.xml"),
    (r"^files/org\.openmw\.(cs|launcher)\.desktop$", r"files/org.openfallout.\1.desktop"),
    # Only the Windows resource script and manifest of the engine moved; openmw.ico and openmw-wizard.rc stayed.
    (r"^files/windows/openmw\.(rc|exe\.manifest)$", r"files/windows/openfallout.\1"),
    (r"^files/mac/openmw(-cs)?-Info\.plist\.in$", r"files/mac/openfallout\1-Info.plist.in"),
    (r"^cmake/OpenMWMacros\.cmake$", "cmake/OpenFalloutMacros.cmake"),
))


# ---- Names inside files ---------------------------------------------------------------------------------------------

# The C++ namespaces of the engine: MWWorld, MWGui and so on. `MWScript` is not in the list, see _MW_SCRIPT.
_NAMESPACES = b"World|Base|Mechanics|Gui|Render|Lua|Physics|Dialogue|Sound|State|Class|Input"
# Test fixtures are named after the namespace they test (`MWWorldWeatherTest`, `MWScriptTest`) and were renamed too.
_MW = re.compile(
    rb"\bMW(" + _NAMESPACES + rb")\b|\bMW(" + _NAMESPACES + rb"|Script)(?=[A-Za-z]*Tests?\b)")
# `MWScript` names the Morrowind script language and was kept (`MusicType::MWScript`, comments, Lua docs), except where
# it is the C++ namespace of the script engine.
# A globally qualified use (`::MWScript::Compiler`) is the namespace too; `Class::MWScript` without a following `::` is not.
_MW_SCRIPT = re.compile(rb"(?<![\w:])MWScript(?=::)|(?<![\w:]::)(?<=::)MWScript(?=::)|(?<=namespace )MWScript\b")
# `OMW` became `OF`: `OMW::Engine`, and the l10n contexts and Lua events of the engine (`OMWEngine`, `OMWConsolePrint`).
# Not renamed: GLSL names (`OMW_HDR`), the names of file formats (`OMWFX`, `OMWScripts`, `OMWAddon`), the storage
# section `OMWInputBindings` (renaming it would drop the key bindings people saved), and names that only contain the
# letters (`SettingsOMWCombat`). The CMake macro variables `OMW_ADD_EXE_*` were renamed.
_OMW = re.compile(rb"\bOMW(?!FX\b|Scripts\b|Addon\b|Ayembedt|InputBindings\b|_(?!ADD_EXE))(?=[A-Z:_]|\b)")

# What keeps "openmw" or "OpenMW" because it names something that was not renamed: artwork, widget layouts, the
# Morrowind-era manuals, web addresses and packages of the OpenMW project, real OpenMW releases, the Qt resource aliases
# of the editor, and a few names that the rename left alone.
_KEEP = (
    rb"OPENMW CODE (?:STARTS|ENDS) HERE"  # comments in extern/oics that mark OpenMW's own changes to that library
    rb"|OpenMW(?= \d+\.\d+)"  # "OpenMW 0.48.0": a real release, which can still read the saves of that time
    rb"|OpenMW PPA|OpenMW-VR|OpenMW Unit Test"
    rb"|https?://[A-Za-z0-9.]*(?:gitlab\.com/OpenMW|openmw\.org|openmw\.readthedocs\.io|openmw\.sourceforge\.net)"
    rb"[^\s\"')>\]`]*"
    rb"|[A-Za-z0-9.]*openmw\.(?:org|readthedocs\.io|sourceforge\.net)[^\s\"')>\]`]*"
    rb"|gitlab\.com/OpenMW/[^\s\"')>\]`]*"
    rb"|openmw_[A-Za-z0-9_]*(?:\.skin)?\.(?:layout|xml)"  # widget layouts and skins of MyGUI (files/data/mygui)
    rb"|openmw[_-][A-Za-z0-9_-]*\.(?:png|ico|icns|svg|webm|bmp|dds)"  # artwork
    rb"|openmw(?:-cs|-wizard)?\.(?:png|ico|icns|svg|webm|bmp)|openmwcs\.(?:jpg|webp)|openmw-wizard\.rc"
    # Qt resource aliases and Inkscape ids of the editor icons (files/opencs).
    rb'|:(?:run-|stop-)?openmw(?:-cs)?(?=")|(?<=alias=")(?:run-|stop-)?openmw(?:-cs)?'
    rb'|(?<=id=")(?:run|stop)-openmw|(?<=label=")(?:run|stop)-openmw'
    rb"|openmw-cs/[a-z]|openmw-(?:game-template|docs|deps|tes3mp)|install-openmw|openmwcfg"  # the manuals, a lexer name
    rb"|ppa:openmw/openmw|openmw-@"  # the package archive, the address of the documentation (components/version)
    rb"|[A-Z_]{4,}\s*=\s*org\.openmw(?!\.)"  # bundle ids in docs/Doxyfile.cmake
    rb"|openmw_binary"  # a variable of scripts/integration_tests.py
    rb"|apps\\+openmw(?=\\)"  # the folder names of a CMake source group, written with backslashes
    rb"|/home/user/openmw/"  # sample paths in the option parser tests
)
# The bug tracker in the crash messages and dialogs moved to OpenFallout's repository (a link without the `/-/` of
# GitLab), while the links to single OpenMW issues in comments stayed.
_ISSUES = rb"https://gitlab\.com/OpenMW/openmw/issues(?![\w/-])"
# This is the address the tree has NOW (the repository moved to OpenBethesda after the rename, whose day-one value was
# Elemperor1/OpenFallout): the translation is merged onto the current tree, so it must produce what that tree says.
_ISSUES_NEW = b"https://github.com/OpenBethesda/OpenFallout/issues"
_REPLACEMENT = {"up": b"OPENFALLOUT", "camel": b"OpenFallout", "low": b"openfallout", "issues": _ISSUES_NEW}
# OpenMW as a word (not `TestingOpenMW`, `OpenMWs` or `OpenMW_Tests`: the rename matched whole words), the CMake
# variables and include names that are built from it, and openmw as a lower-case name (not `openmwluadocumentor`).
_CAMEL = rb"(?<![A-Za-z0-9_])OpenMW(?![A-Za-z0-9_])|OpenMW(?=_(?:BINARY|SOURCE)_DIR\b|Macros\b)"
_LOW = rb"(?<![A-Za-z])openmw(?![a-z0-9])"
_TOKENS = re.compile(
    rb"(?P<issues>" + _ISSUES + rb")|(?P<keep>" + _KEEP + rb")|(?P<up>OPENMW)|(?P<camel>" + _CAMEL + rb")|(?P<low>"
    + _LOW + rb")")
# In a raw string (help texts, embedded Lua scripts) the program and module names changed, not the prose.
_TOKENS_RAW = re.compile(
    rb"(?P<issues>" + _ISSUES + rb")|(?P<keep>" + _KEEP + rb")|(?P<up>OPENMW)|(?P<low>(?<![A-Za-z])openmw(?=[.\-_]))")

# ---- Kinds of file --------------------------------------------------------------------------------------------------

# Kept as they were: OpenMW's history and credits, the notes about OpenMW's first code, the LaTeX manual of the editor,
# and the requirements of the documentation build (they name a branch of somebody else's repository).
_FROZEN = re.compile(
    r"^(?:CHANGELOG\.md|AUTHORS\.md|LICENSE|docs/openmw-stage1\.md|manual/.*|docs/requirements\.txt)$")
# They keep "OpenMW" and "MW" in prose, because they describe OpenMW or Morrowind: only file names, module names and
# `OPENMW_*` in them were renamed (`openmw.cfg`, `require('openmw.core')`).
_PROSE = re.compile(
    r"^(?:docs/source/(?!conf\.py$|index\.rst$).*|docs/README\.md|\.gitlab/.*|files/lua_api/.*"
    r"|files/data/(?:shaders|fonts|animations)/.*|apps/openmw/mwscript/docs/.*|extern/.*"
    r"|cmake/FindLIBUNSHIELD\.cmake|CONTRIBUTING\.md)$")
_PROSE_NAMES = re.compile(
    rb"(?P<keep>" + _KEEP + rb")|(?P<up>OPENMW)"
    rb"|(?P<low>openmw(?=\.(?:cfg|log)\b|\.[a-z]|_aux|_[a-z]+\.html|_[a-z]+>|/(?:saves|screenshots|data|``)|/\*lua"
    rb"|-cs\b|``))")
# The pages of the Lua API quote `openmw.cfg` as a file of OpenMW and refer to modules as `@{openmw_core#...}`.
_LUADOC_NAMES = re.compile(
    rb"(?P<keep>" + _KEEP + rb")|(?P<up>OPENMW)"
    rb"|(?P<low>openmw(?=\.(?!cfg\b|log\b)[a-z]|_aux|_[a-z]+[#>]|/[a-z]+\.lua))")
# Build and CI files name OpenMW's package archives, dependency bundles and project path, which are not ours, so only
# what the build produces was renamed (`openmw-tests`, `OpenMW-CS.app`) and the names of workflow steps.
_CI = re.compile(r"^(?:\.github/.*|\.gitlab-ci\.yml|CI/.*|docker/Dockerfile.*)$")
_CI_KEEP = (
    rb"openmw-(?:deps|android-deps|manifest|package|coverage|integration-tests|clang-format|qt-translations|ubuntu"
    rb"|daily|sym)[A-Za-z-]*|openmw-\*\*|ppa:openmw/[a-z-]+|OpenMW/openmw|(?<=recommends )openmw(?: openmw-launcher)?"
    rb"|OpenMW_MSVC2022_64_|Ubuntu_Clang_Tidy_openmw"
)
_CI_TOKENS = re.compile(
    rb"(?P<keep>" + _CI_KEEP + rb"|" + _KEEP + rb")|(?P<up>OPENMW)|(?P<low>" + _LOW + rb")"
    rb"|(?P<camel>OpenMW(?=-CS|\.app|-\*))")
_CI_STEP = re.compile(rb"^(\s*- name:.*)$", re.M)
_CPP = (".cpp", ".hpp", ".h", ".cc", ".c", ".inl", ".mm")
# In C++ the rename touched what a person sees (string literals) and left comments and identifiers alone, except for
# these: the paths of includes, the names `openmw.cfg` and `openmw.log`, and identifiers such as `openmwCfgFile`.
# A character literal (`'"'`, `'\\''`) is one token that is skipped, so its quote does not start a string. A quote right
# after a word character or a quote is a digit separator (`1'000'000`), not a character literal.
_STRING = re.compile(
    rb"(?P<chr>(?<![\w'])(?:u8|[uUL])?'(?:\\[^\n'][^'\n]{0,8}|\\'|[^'\\\n])')"
    rb'|(?P<raw>R"(?P<delim>[^()\s\\]{0,16})\((?s:.*?)\)(?P=delim)")|"(?:[^"\\\n]|\\.)*"')
_APPS = re.compile(rb"(?P<url>https?://[^\s\"')>\]`]*)|\bapps/openmw(?P<tests>_tests)?(?![A-Za-z0-9_-])")
_FILE = re.compile(rb"\bopenmw(?=(?:-cs)?\.(?:cfg|log)\b)")
_IDENT = re.compile(rb"\bopenmw(?=[A-Z])")
# Two test files use the string "openmw" as made-up data (the name of a temporary folder, the first word of a command
# line that the option parser is asked to read), and kept it.
_TEST_SCAFFOLDING = ("components/testing/util.hpp", "apps/openmw_tests/options.cpp")
_MENTIONS = re.compile(rb"MW|openmw")
_RST_UNDERLINE = re.compile(rb"^([=\-~^\"#*+`.:'_])\1+$")


def _rename(match):
    if match.group("keep"):
        return match.group(0)
    return _REPLACEMENT[match.lastgroup]


def _rename_apps(match):
    if match.group("url"):
        return match.group(0)
    return b"apps/openfallout" + (match.group("tests") or b"")


def _rename_in_strings(match):
    if match.group("chr"):
        return match.group(0)
    tokens = _TOKENS_RAW if match.group("raw") else _TOKENS
    return tokens.sub(_rename, match.group(0))


def _fix_rst_underlines(old, new):
    """Draw the underline of a renamed title again, because the title became longer (the rename did the same)."""
    old_lines = old.split(b"\n")
    new_lines = new.split(b"\n")
    if len(old_lines) != len(new_lines):
        return new
    for i in range(1, len(new_lines)):
        title = new_lines[i - 1]
        # An underline that was long enough for the old title (upstream sometimes draws it longer) gets the length of
        # the new title, but only when the rename changed the title.
        if (_RST_UNDERLINE.match(old_lines[i]) and old_lines[i - 1].strip() and title != old_lines[i - 1]
                and len(old_lines[i]) >= len(old_lines[i - 1]) and _RST_UNDERLINE.match(new_lines[i])):
            new_lines[i] = old_lines[i][:1] * len(title)
    return b"\n".join(new_lines)


def translate_path(path):
    """The OpenFallout path of an upstream path (the same path when no rule names it)."""
    for pattern, replacement in PATH_RULES:
        if pattern.match(path):
            return pattern.sub(replacement, path, count=1)
    return path


def _translate_cpp(path, text):
    text = _MW_SCRIPT.sub(b"OFScript", text)
    text = _IDENT.sub(b"openfallout", _FILE.sub(b"openfallout", text))
    text = text.replace(b"OPENMW_", b"OPENFALLOUT_")
    if path in _TEST_SCAFFOLDING:
        text = text.replace(b'"openmw"', b'"\x01"')  # hide the literal from the string rule, and give it back below
    text = _STRING.sub(_rename_in_strings, text)
    return text.replace(b'"\x01"', b'"openmw"')


def translate_content(path, data):
    """The OpenFallout text of an upstream file. `path` is the upstream path, `data` the file's bytes.

    A binary file (a NUL byte in its first 8000 bytes, the test git uses) comes back unchanged.
    """
    # Every rule needs one of these letter groups, so a file without them (most of them) is returned at once.
    if b"\0" in data[:8000] or _FROZEN.match(path) or not _MENTIONS.search(data):
        return data
    if _PROSE.match(path):
        names = _LUADOC_NAMES if path.startswith("files/lua_api/") else _PROSE_NAMES
        text = data if path.startswith("docs/") else _OMW.sub(b"OF", data)  # `#{OMWShaders:...}` in a shader
        result = names.sub(_rename, _APPS.sub(_rename_apps, text))
    else:
        text = _OMW.sub(b"OF", _MW.sub(lambda m: b"OF" + (m.group(1) or m.group(2)), data))
        text = _APPS.sub(_rename_apps, text)
        if path.endswith(_CPP):
            result = _translate_cpp(path, text)
        elif _CI.match(path):
            result = _CI_STEP.sub(lambda m: _TOKENS.sub(_rename, m.group(1)), _CI_TOKENS.sub(_rename, text))
        else:
            result = _TOKENS.sub(_rename, text)
    if path.endswith((".rst", ".md")) and result != data:
        result = _fix_rst_underlines(data, result)
    return result


# ---- Measuring ------------------------------------------------------------------------------------------------------

def _read_tree(repo, rev):
    """{path: bytes} of every file of a revision, read through one `git cat-file --batch` process."""
    listing = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "-z", rev], check=True, capture_output=True).stdout
    entries = []
    for line in listing.split(b"\0"):
        if not line:
            continue
        meta, path = line.split(b"\t", 1)
        mode, kind, sha = meta.split()
        if kind == b"blob" and mode != b"160000":
            entries.append((path.decode("utf-8", "surrogateescape"), sha))
    tree = {}
    with subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE) as proc:
        # Writing every request first would fill the pipe while we do not read, so ask in blocks.
        for start in range(0, len(entries), 200):
            block = entries[start:start + 200]
            proc.stdin.write(b"".join(sha + b"\n" for _, sha in block))
            proc.stdin.flush()
            for path, _ in block:
                _, _, size = proc.stdout.readline().split()
                tree[path] = proc.stdout.read(int(size))
                proc.stdout.read(1)
        proc.stdin.close()
    return tree


def _first_difference(a, b):
    """The first line that differs between two byte strings, as (line number, line of a, line of b)."""
    la, lb = a.split(b"\n"), b.split(b"\n")
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else b"<end of file>"
        y = lb[i] if i < len(lb) else b"<end of file>"
        if x != y:
            return i + 1, x, y
    return 0, b"", b""


def measure(base, renamed, show_list, repo="."):
    """Translate every file of BASE and compare with RENAMED. Returns the paths that differ."""
    old, new = _read_tree(repo, base), _read_tree(repo, renamed)
    present = 0
    same = 0
    differing = []
    for path, data in old.items():
        target = translate_path(path)
        if target not in new:
            continue
        present += 1
        if translate_content(path, data) == new[target]:
            same += 1
        else:
            differing.append((path, target))
    print("files in %s: %d" % (base, len(old)))
    print("translated path exists in %s: %d" % (renamed, present))
    print("byte-identical after translation: %d" % same)
    print("different: %d" % (present - same))
    if show_list:
        for path, target in sorted(differing):
            n, x, y = _first_difference(translate_content(path, old[path]), new[target])
            print("%s -> %s\n  line %d\n  translated: %s\n  renamed:    %s"
                  % (path, target, n, x[:200].decode("utf-8", "replace"), y[:200].decode("utf-8", "replace")))
    return differing


def main(argv):
    if len(argv) >= 4 and argv[1] == "--measure":
        measure(argv[2], argv[3], "--list" in argv[4:])
        return 0
    print("usage: upstream_translate.py --measure BASE RENAMED [--list]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
