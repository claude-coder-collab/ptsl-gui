#!/usr/bin/env python3
"""Build a self-contained, ad-hoc signed PTSL GUI.app for macOS 13.3+ (arm64) in dist/.

Uses the `dist` CMake preset: dependencies from Conan (tools/conan/macos13 profile) and the official Qt
binaries (installed with aqtinstall), so every binary in the bundle runs on macOS 13.3.

The bundle embeds the Avid SDK's PTSL.proto and client framework: it is for personal use and must not be published.
"""

from __future__ import annotations

import argparse
import os
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
APP_NAME = "PTSL GUI.app"
ARCH = "arm64"
MINIMUM_MACOS = "13.3"
QT_VERSION = "6.11.3"
QT_ROOT = Path.home() / "Qt"
QT_PREFIX = QT_ROOT / QT_VERSION / "macos"
VENV = Path.home() / ".ptsl-venv"
CONAN_HOME = Path.home() / ".ptslconan"
CONAN_OUTPUT = PROJECT_ROOT / "build" / "conan-dist"
CONAN_PROFILE = PROJECT_ROOT / "tools" / "conan" / "macos13"
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


def version_tuple(text: str) -> tuple[int, ...]:
    return tuple(int(part) for part in text.split("."))


def minimum_macos(otool_l_output: str) -> str | None:
    """Highest minimum macOS among the load commands of `otool -l` output (all architectures)."""
    versions: list[str] = []
    in_version_command = False
    for line in otool_l_output.splitlines():
        stripped = line.strip()
        if stripped.startswith("cmd "):
            in_version_command = stripped in ("cmd LC_BUILD_VERSION", "cmd LC_VERSION_MIN_MACOSX")
        elif in_version_command and (match := re.fullmatch(r"(minos|version) (\d+(?:\.\d+)*)", stripped)):
            versions.append(match.group(2))
            in_version_command = False
    return max(versions, key=version_tuple) if versions else None


def missing_rpath_libraries(libraries: list[str], frameworks: Path) -> list[str]:
    """@rpath references that are not present in the bundle's Frameworks directory."""
    prefix = "@rpath/"
    return [
        library
        for library in libraries
        if library.startswith(prefix) and not (frameworks / library.removeprefix(prefix)).exists()
    ]


def tool_output(*command: str) -> str:
    return subprocess.run(list(command), check=True, capture_output=True, text=True).stdout


def dependencies(path: Path) -> list[str]:
    own_name = install_name(tool_output("otool", "-D", str(path)))
    return [library for library in linked_libraries(tool_output("otool", "-L", str(path))) if library != own_name]


def machos(bundle: Path) -> list[Path]:
    return [path for path in sorted(bundle.rglob("*")) if is_macho(path)]


def thin_to_arch(bundle: Path, arch: str = ARCH) -> int:
    """Removes other architectures from universal binaries; returns how many files were thinned."""
    thinned = 0
    for path in machos(bundle):
        archs = tool_output("lipo", "-archs", str(path)).split()
        if len(archs) > 1 and arch in archs:
            subprocess.run(["lipo", "-thin", arch, str(path), "-output", str(path)], check=True)
            thinned += 1
    return thinned


def prune_broken_plugins(bundle: Path) -> tuple[list[str], dict[str, list[str]]]:
    """Removes plugins whose @rpath dependencies were not deployed; returns removed plugins and other broken binaries."""
    frameworks = bundle / "Contents" / "Frameworks"
    plugins = bundle / "Contents" / "PlugIns"
    removed: list[str] = []
    broken: dict[str, list[str]] = {}
    for path in machos(bundle):
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
    for path in machos(bundle):
        external = external_references(dependencies(path))
        if external:
            problems[str(path.relative_to(bundle))] = external
    return problems


def find_too_new(bundle: Path, minimum: str = MINIMUM_MACOS) -> dict[str, str]:
    """Binaries whose minimum macOS is newer than `minimum` (or unknown)."""
    problems: dict[str, str] = {}
    for path in machos(bundle):
        found = minimum_macos(tool_output("otool", "-l", str(path)))
        if found is None or version_tuple(found) > version_tuple(minimum):
            problems[str(path.relative_to(bundle))] = found or "unknown"
    return problems


def plist_minimum_macos(bundle: Path) -> str | None:
    with (bundle / "Contents" / "Info.plist").open("rb") as handle:
        return plistlib.load(handle).get("LSMinimumSystemVersion")


def run(command: list[str], env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=PROJECT_ROOT, check=True, env=env)


def build(run_tests: bool) -> None:
    if not (QT_PREFIX / "bin" / "macdeployqt").exists():
        run([str(VENV / "bin" / "aqt"), "install-qt", "mac", "desktop", QT_VERSION, "clang_64", "--outputdir",
             str(QT_ROOT)])
    run(
        [str(VENV / "bin" / "conan"), "install", ".", "--output-folder", str(CONAN_OUTPUT), "--profile:all",
         str(CONAN_PROFILE), "--build=missing", "--remote", "conancenter"],
        env=dict(os.environ, CONAN_HOME=str(CONAN_HOME)),
    )
    run(["cmake", "--preset", "dist"])
    run(["cmake", "--build", "--preset", "dist"])
    if run_tests:
        run(["ctest", "--preset", "dist"])


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", type=Path, default=PROJECT_ROOT / "dist")
    parser.add_argument("--skip-build", action="store_true", help="Package the existing dist build")
    parser.add_argument("--skip-tests", action="store_true", help="Do not run the test suite of the dist build")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.skip_build:
        build(run_tests=not args.skip_tests)

    source = PROJECT_ROOT / "build" / "dist" / "app" / "Release" / APP_NAME
    if not source.exists():
        print(f"error: {source} not found; build the dist preset first", file=sys.stderr)
        return 1
    args.output.mkdir(parents=True, exist_ok=True)
    bundle = args.output / APP_NAME
    if bundle.exists():
        shutil.rmtree(bundle)
    run(["ditto", str(source), str(bundle)])
    run([str(QT_PREFIX / "bin" / "macdeployqt"), str(bundle), "-always-overwrite"])
    print(f"thinned {thin_to_arch(bundle)} universal binaries to {ARCH}")

    removed, broken = prune_broken_plugins(bundle)
    for plugin in removed:
        print(f"removed plugin with undeployed dependencies: {plugin}")
    failures = [f"{binary} needs undeployed {', '.join(libraries)}" for binary, libraries in broken.items()]
    failures += [
        f"{binary} still links {', '.join(libraries)}" for binary, libraries in find_external_references(bundle).items()
    ]
    failures += [f"{binary} requires macOS {version}" for binary, version in find_too_new(bundle).items()]
    if (plist_version := plist_minimum_macos(bundle)) != MINIMUM_MACOS:
        failures.append(f"Info.plist LSMinimumSystemVersion is {plist_version}, expected {MINIMUM_MACOS}")
    if failures:
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        return 1

    run(["codesign", "--force", "--deep", "--sign", "-", str(bundle)])
    run(["codesign", "--verify", "--deep", "--strict", str(bundle)])
    print(f"Packaged {bundle} (macOS {MINIMUM_MACOS}+, {ARCH})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
