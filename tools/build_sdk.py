#!/usr/bin/env python3
"""Build the Avid PTSL C++ SDK client library (and optionally ptslcmd) on macOS for arm64.

Works around issues with the SDK's own build script:
- the Conan cache is kept outside the project so paths with spaces don't break OpenSSL;
- the macOS version passed to Conan is clamped to one Conan knows about;
- Ninja Multi-Config is used instead of Xcode (Command Line Tools are enough);
- Conan's protoc/grpc_cpp_plugin are put first on PATH so a newer system protoc isn't picked up;
- dependencies are built for macOS 13.3, with configure checks overridden for functions that are only
  declared in the newer macOS SDK (they would otherwise be linked and fail to load on older macOS).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_VENV = Path.home() / ".ptsl-venv"
DEFAULT_CONAN_HOME = Path.home() / ".ptslconan"
ARCH = "arm64"
MINIMUM_MACOS = "13.3"
CONAN_CONF_LINES = ('c-ares/*:tools.cmake.cmaketoolchain:extra_variables={"HAVE_PIPE2": "0"}',)
PACKAGE_FOLDER_RE = re.compile(r'set\((\w+)_PACKAGE_FOLDER_RELEASE "([^"]+)"\)')
SDK_DIR_PATTERN = re.compile(r"^PTSL_SDK_CPP\.(\d+)\.(\d+)\.(\d+)\.(\d+)$")


def find_sdk_dir(root: Path) -> Path:
    candidates: list[tuple[tuple[int, ...], Path]] = []
    for entry in root.iterdir():
        match = SDK_DIR_PATTERN.match(entry.name)
        if match and entry.is_dir():
            candidates.append((tuple(int(part) for part in match.groups()), entry))
    if not candidates:
        raise FileNotFoundError(f"No PTSL_SDK_CPP.* directory found in {root}")
    return max(candidates)[1]


def use_ninja_generator(presets_file: Path) -> None:
    presets = json.loads(presets_file.read_text())
    for preset in presets.get("configurePresets", []):
        inherits = preset.get("inherits", [])
        preset["inherits"] = ["ninja" if name == "xcode" else name for name in inherits]
    presets_file.write_text(json.dumps(presets, indent=4))


def package_folders(dependencies_dir: Path) -> dict[str, Path]:
    """Conan package folders of a source dir's dependencies, read from the CMakeDeps data files."""
    folders: dict[str, Path] = {}
    for data_file in sorted(dependencies_dir.glob("*-release-*-data.cmake")):
        for name, folder in PACKAGE_FOLDER_RE.findall(data_file.read_text()):
            folders[name] = Path(folder)
    return folders


def ensure_conan_conf(conan_home: Path, lines: tuple[str, ...] = CONAN_CONF_LINES) -> None:
    conf = conan_home / "global.conf"
    existing = conf.read_text().splitlines() if conf.exists() else []
    missing = [line for line in lines if line not in existing]
    if missing:
        conan_home.mkdir(parents=True, exist_ok=True)
        conf.write_text("\n".join([*existing, *missing]) + "\n")


def ensure_venv(venv: Path, requirements: Path) -> Path:
    python = venv / "bin" / "python"
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "-q", "-r", str(requirements)], check=True)
    return python


def build_target(
    sdk: Path, source_dir: Path, python: Path, conan_home: Path, config: str, os_version: str
) -> None:
    env = dict(os.environ)
    env["CONAN_HOME"] = str(conan_home)
    env["PTSL_OS_VERSION"] = os_version
    env["PATH"] = f"{python.parent}{os.pathsep}{env['PATH']}"

    presets_file = source_dir / "CMakeUserPresets.json"
    presets_file.unlink(missing_ok=True)
    subprocess.run(
        [
            str(python),
            str(sdk / "Config" / "ptsl_build_script.py"),
            "--source_dir",
            str(source_dir),
            "--config",
            config,
            "--arch",
            ARCH,
            "--library_type",
            "shared",
        ],
        cwd=sdk / "Config",
        env=env,
        check=False,
    )

    if not presets_file.exists():
        raise RuntimeError(f"Conan step failed for {source_dir}; no CMakeUserPresets.json was generated")
    use_ninja_generator(presets_file)

    build_dir = source_dir / "MacBuild" / ARCH
    for stale in ("CMakeCache.txt", "CMakeFiles"):
        target = build_dir / stale
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
            target.unlink()

    folders = package_folders(build_dir / "Dependencies")
    tool_dirs = [str(folders[name] / "bin") for name in ("protobuf", "grpc") if name in folders]
    env["PATH"] = os.pathsep.join([*tool_dirs, env["PATH"]])
    env["PTSLC_CPP_FIND_BUILD_TYPE"] = config
    preset = f"ptsl-Darwin-{ARCH}-{config}"
    subprocess.run(["cmake", "--workflow", "--preset", preset], cwd=source_dir, env=env, check=True)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sdk", type=Path, help="SDK directory (default: newest PTSL_SDK_CPP.* in project root)")
    parser.add_argument("--config", default="Release", choices=["Debug", "Release"])
    parser.add_argument("--os-version", default=MINIMUM_MACOS, help="Minimum macOS (default: %(default)s)")
    parser.add_argument("--venv", type=Path, default=DEFAULT_VENV)
    parser.add_argument("--conan-home", type=Path, default=DEFAULT_CONAN_HOME)
    parser.add_argument("--with-ptslcmd", action="store_true", help="Also build the ptslcmd example")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    sdk = (args.sdk or find_sdk_dir(PROJECT_ROOT)).resolve()
    if " " in str(args.conan_home):
        raise ValueError("Conan home must not contain spaces")
    python = ensure_venv(args.venv, sdk / "Config" / "requirements.txt")
    ensure_conan_conf(args.conan_home)
    os_version = args.os_version

    targets = [sdk]
    if args.with_ptslcmd:
        targets.append(sdk / "examples" / "ptslcmd")
    for source_dir in targets:
        build_target(sdk, source_dir, python, args.conan_home, args.config, os_version)

    print(f"Installed to {sdk / 'install' / ARCH / args.config}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
