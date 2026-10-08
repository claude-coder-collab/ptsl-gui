#!/usr/bin/env python3
"""Run all local checks: pytest, clang-format, and build + test for each CMake preset."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
ALL_PRESETS = ("dev", "asan", "tsan", "tidy")
TESTED_PRESETS = {"dev", "asan", "tsan"}


def source_files(root: Path) -> list[str]:
    output = subprocess.run(
        ["git", "ls-files", "-co", "--exclude-standard", "-z", "--", "*.cpp", "*.hpp"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return [name for name in output.split("\0") if name]


def steps(presets: list[str], files: list[str], python: str, fix_format: bool) -> list[tuple[str, list[str]]]:
    format_args = ["-i"] if fix_format else ["--dry-run", "--Werror"]
    result: list[tuple[str, list[str]]] = [
        ("pytest", [python, "-m", "pytest", "-q", "tests/tools"]),
        ("clang-format", ["clang-format", *format_args, *files]),
    ]
    for preset in presets:
        result.append((f"configure {preset}", ["cmake", "--preset", preset]))
        result.append((f"build {preset}", ["cmake", "--build", "--preset", preset]))
        if preset in TESTED_PRESETS:
            result.append((f"test {preset}", ["ctest", "--preset", preset]))
    return result


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", action="append", choices=ALL_PRESETS, help="Limit to these presets (repeatable)")
    parser.add_argument("--fix-format", action="store_true", help="Reformat files instead of checking")
    parser.add_argument("--keep-going", action="store_true", help="Run every step even after a failure")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    presets = args.preset or list(ALL_PRESETS)
    failures: list[str] = []
    for name, command in steps(presets, source_files(PROJECT_ROOT), sys.executable, args.fix_format):
        print(f"==> {name}", flush=True)
        if subprocess.run(command, cwd=PROJECT_ROOT, check=False).returncode != 0:
            failures.append(name)
            if not args.keep_going:
                break
    if failures:
        print(f"FAILED: {', '.join(failures)}", file=sys.stderr)
        return 1
    print("All checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
