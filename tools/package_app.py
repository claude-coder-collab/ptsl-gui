#!/usr/bin/env python3
"""Build a self-contained, ad-hoc signed PTSL GUI.app in dist/ (macOS, arm64).

The bundle embeds the Avid SDK's PTSL.proto and client framework: it is for personal use and must not be published.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
APP_NAME = "PTSL GUI.app"
MACHO_MAGICS = {
    bytes.fromhex("cffaedfe"),
    bytes.fromhex("feedfacf"),
    bytes.fromhex("cefaedfe"),
    bytes.fromhex("feedface"),
    bytes.fromhex("cafebabe"),
}
EXTERNAL_PREFIXES = ("/opt/homebrew/", "/usr/local/")


def is_macho(path: Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as handle:
        return handle.read(4) in MACHO_MAGICS


def linked_libraries(otool_output: str) -> list[str]:
    """Parse `otool -L` output into the list of referenced library paths."""
    libraries: list[str] = []
    for line in otool_output.splitlines()[1:]:
        line = line.strip()
        if line and not line.endswith(":"):
            libraries.append(line.split(" (", 1)[0])
    return libraries


def external_references(libraries: list[str]) -> list[str]:
    return [library for library in libraries if library.startswith(EXTERNAL_PREFIXES)]


def install_name(otool_d_output: str) -> str | None:
    """Parse `otool -D` output: the binary's own install name, if it is a library."""
    lines = [line.strip() for line in otool_d_output.splitlines()[1:] if line.strip()]
    return lines[0] if lines else None


def qt_library_paths(prefix: Path = Path("/opt/homebrew/opt")) -> list[Path]:
    """Homebrew installs each Qt module in its own keg; macdeployqt needs all of their lib directories."""
    return sorted(path / "lib" for path in prefix.glob("qt*") if (path / "lib").is_dir())


def missing_rpath_libraries(libraries: list[str], frameworks: Path) -> list[str]:
    """@rpath references that are not present in the bundle's Frameworks directory."""
    prefix = "@rpath/"
    return [
        library
        for library in libraries
        if library.startswith(prefix) and not (frameworks / library.removeprefix(prefix)).exists()
    ]


def dependencies(path: Path) -> list[str]:
    output = subprocess.run(["otool", "-L", str(path)], check=True, capture_output=True, text=True).stdout
    own_name = install_name(subprocess.run(["otool", "-D", str(path)], check=True, capture_output=True, text=True).stdout)
    return [library for library in linked_libraries(output) if library != own_name]


def prune_broken_plugins(bundle: Path) -> tuple[list[str], dict[str, list[str]]]:
    """Removes plugins whose @rpath dependencies were not deployed; returns removed plugins and other broken binaries."""
    frameworks = bundle / "Contents" / "Frameworks"
    plugins = bundle / "Contents" / "PlugIns"
    removed: list[str] = []
    broken: dict[str, list[str]] = {}
    for path in sorted(bundle.rglob("*")):
        if not is_macho(path):
            continue
        missing = missing_rpath_libraries(dependencies(path), frameworks)
        if not missing:
            continue
        if path.is_relative_to(plugins):
            path.unlink()
            removed.append(str(path.relative_to(bundle)))
        else:
            broken[str(path.relative_to(bundle))] = missing
    return removed, broken


def find_external_references(bundle: Path) -> dict[str, list[str]]:
    problems: dict[str, list[str]] = {}
    for path in sorted(bundle.rglob("*")):
        if not is_macho(path):
            continue
        external = external_references(dependencies(path))
        if external:
            problems[str(path.relative_to(bundle))] = external
    return problems


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=PROJECT_ROOT, check=True)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=PROJECT_ROOT / "dist")
    parser.add_argument("--skip-build", action="store_true", help="Package the existing Release build")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.skip_build:
        run(["cmake", "--preset", "dev"])
        run(["cmake", "--build", "--preset", "dev-release", "--target", "ptslgui_app"])

    source = PROJECT_ROOT / "build" / "dev" / "app" / "Release" / APP_NAME
    if not source.exists():
        print(f"error: {source} not found; build the Release app first", file=sys.stderr)
        return 1
    args.output.mkdir(parents=True, exist_ok=True)
    bundle = args.output / APP_NAME
    if bundle.exists():
        shutil.rmtree(bundle)
    run(["ditto", str(source), str(bundle)])
    run(["macdeployqt", str(bundle), "-always-overwrite", *[f"-libpath={path}" for path in qt_library_paths()]])
    removed, broken = prune_broken_plugins(bundle)
    for plugin in removed:
        print(f"removed plugin with undeployed dependencies: {plugin}")
    if broken:
        for binary, libraries in broken.items():
            print(f"error: {binary} needs undeployed {', '.join(libraries)}", file=sys.stderr)
        return 1
    run(["codesign", "--force", "--deep", "--sign", "-", str(bundle)])
    run(["codesign", "--verify", "--deep", "--strict", str(bundle)])

    problems = find_external_references(bundle)
    if problems:
        for binary, libraries in problems.items():
            print(f"error: {binary} still links {', '.join(libraries)}", file=sys.stderr)
        return 1
    print(f"Packaged {bundle}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
