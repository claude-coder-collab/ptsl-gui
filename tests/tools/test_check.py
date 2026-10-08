import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check


def test_steps_cover_presets_and_only_test_tested_presets() -> None:
    names = [name for name, _ in check.steps(["dev", "tidy"], ["a.cpp"], "python", fix_format=False)]
    assert names == [
        "pytest",
        "clang-format",
        "configure dev",
        "build dev",
        "test dev",
        "configure tidy",
        "build tidy",
    ]


def test_format_step_checks_or_fixes() -> None:
    check_step = dict(check.steps([], ["a.cpp", "b.hpp"], "python", fix_format=False))["clang-format"]
    fix_step = dict(check.steps([], ["a.cpp"], "python", fix_format=True))["clang-format"]
    assert check_step == ["clang-format", "--dry-run", "--Werror", "a.cpp", "b.hpp"]
    assert fix_step == ["clang-format", "-i", "a.cpp"]


def test_source_files_lists_tracked_cpp_files() -> None:
    files = check.source_files(check.PROJECT_ROOT)
    assert "core/src/schema.cpp" in files
    assert all(name.endswith((".cpp", ".hpp")) for name in files)


def test_parse_args_defaults() -> None:
    args = check.parse_args([])
    assert (args.preset, args.fix_format, args.keep_going) == (None, False, False)
