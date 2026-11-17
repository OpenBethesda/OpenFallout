#!/usr/bin/env python3
"""The OpenGL ES 3 OpenSceneGraph build: what it builds and when it has to be built again.

The libraries and plugins come from the engine's own lists (USED_OSG_COMPONENTS and USED_OSG_PLUGINS in the root
CMakeLists.txt), so the two cannot drift apart. The cache key covers everything that changes the result.

    osg_gles3.py skip                  the libraries that are not built, separated by ';'
    osg_gles3.py plugin-options        one -DBUILD_OSG_PLUGIN_<NAME>=ON per line
    osg_gles3.py key [--input FILE]...   the cache key (SHA-256); the inputs are files outside this folder that change
                                         the result: the ANGLE version, the dependency set's manifest

Run it from the repository root, or pass --root.
"""

import argparse
import hashlib
from pathlib import Path
import re
import sys

# What each OpenSceneGraph library links (src/<library>/CMakeLists.txt, TARGET_LIBRARIES). osg, osgDB and OpenThreads
# are always built.
LIBRARY_LINKS = {
    "osgUtil": [],
    "osgGA": ["osgUtil"],
    "osgText": ["osgUtil"],
    "osgViewer": ["osgGA", "osgText", "osgUtil"],
    "osgAnimation": ["osgViewer", "osgText", "osgGA", "osgUtil"],
    "osgFX": ["osgUtil"],
    "osgParticle": ["osgUtil"],
    "osgShadow": ["osgUtil"],
    "osgSim": ["osgText", "osgUtil"],
    "osgTerrain": ["osgUtil"],
    "osgVolume": ["osgGA", "osgUtil"],
    "osgManipulator": ["osgViewer", "osgGA", "osgUtil"],
    "osgUI": ["osgViewer", "osgGA", "osgText", "osgUtil"],
    "osgWidget": ["osgViewer", "osgText", "osgGA"],
    "osgPresentation": ["osgViewer", "osgUI", "osgWidget", "osgManipulator", "osgVolume", "osgFX", "osgText", "osgGA",
                        "osgUtil"],
}
ALWAYS_BUILT = ("osg", "osgDB", "OpenThreads")

# The libraries a plugin links besides the always-built ones.
PLUGIN_LINKS = {"dae": ["osgSim", "osgAnimation"], "freetype": ["osgText"]}
# Plugins that have an option BUILD_OSG_PLUGIN_<NAME>. The serializers are built with the libraries and have none.
PLUGINS = ("bmp", "dae", "dds", "freetype", "jpeg", "osg", "png", "tga")
SERIALIZER_PLUGINS = re.compile(r"^osgdb_serializers_\w+$")


def read_version(path):
    """The KEY='value' lines of a version file, which the shell reads with `source`."""
    values = {}
    for line in Path(path).read_text().splitlines():
        match = re.match(r"^([A-Z0-9_]+)='([^']*)'\s*$", line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def engine_lists(cmakelists_text):
    """USED_OSG_COMPONENTS and USED_OSG_PLUGINS of the engine's root CMakeLists.txt."""
    lists = {}
    for key, name in (("components", "USED_OSG_COMPONENTS"), ("plugins", "USED_OSG_PLUGINS")):
        match = re.search(r"set\(\s*" + name + r"\b([^)]*)\)", cmakelists_text)
        if not match:
            raise ValueError(f"{name} is not set in CMakeLists.txt")
        lists[key] = match.group(1).split()
    return lists


def needed_libraries(lists):
    """The libraries to build: what the engine names, what those link, and what the chosen plugins link."""
    needed = set(ALWAYS_BUILT)
    wanted = [name for name in lists["components"]]
    for plugin in lists["plugins"]:
        if plugin.startswith("osgdb_") and plugin[6:] in PLUGIN_LINKS:
            wanted += PLUGIN_LINKS[plugin[6:]]
    while wanted:
        library = wanted.pop()
        if library in needed:
            continue
        if library not in LIBRARY_LINKS:
            raise ValueError(f"unknown OpenSceneGraph component {library}")
        needed.add(library)
        wanted += LIBRARY_LINKS[library]
    return needed


def skipped_libraries(lists):
    needed = needed_libraries(lists)
    return sorted(name for name in LIBRARY_LINKS if name not in needed)


def plugin_options(lists):
    options = []
    for plugin in lists["plugins"]:
        if SERIALIZER_PLUGINS.match(plugin):
            continue
        name = plugin[len("osgdb_"):] if plugin.startswith("osgdb_") else ""
        if name not in PLUGINS:
            raise ValueError(f"unknown OpenSceneGraph plugin {plugin}")
        options.append(f"-DBUILD_OSG_PLUGIN_{name.upper()}=ON")
    return options


def cache_key(root, inputs):
    """A SHA-256 over what decides the result: the commit, the patches, the build script and this helper, the engine's
    two lists and the files named by the caller: the ANGLE version (the library is built against ANGLE's headers) and
    the dependency set's manifest (the picture plugins are linked against its libpng and libjpeg)."""
    root = Path(root)
    folder = root / "CI" / "macos" / "osg"
    digest = hashlib.sha256()

    def add(label, data):
        digest.update(label.encode() + b"\0" + str(len(data)).encode() + b"\0" + data)

    files = [folder / "osg_version.sh", folder / "build_osg_gles3.sh", folder / "osg_gles3.py"]
    files += sorted((folder / "patches").glob("*.patch"))
    files += [Path(path) for path in inputs]
    for path in files:
        add(path.name, path.read_bytes())
    lists = engine_lists((root / "CMakeLists.txt").read_text())
    add("components", " ".join(lists["components"]).encode())
    add("plugins", " ".join(lists["plugins"]).encode())
    return digest.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("skip", "plugin-options", "key"))
    parser.add_argument("--root", default=".", help="the repository root (default: the current folder)")
    parser.add_argument("--input", action="append", default=[],
                        help="a file outside this folder that changes the result (repeat for more than one)")
    args = parser.parse_args(argv)
    root = Path(args.root)
    if args.command == "key":
        print(cache_key(root, args.input))
        return 0
    lists = engine_lists((root / "CMakeLists.txt").read_text())
    if args.command == "skip":
        print(";".join(skipped_libraries(lists)))
    else:
        print("\n".join(plugin_options(lists)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
