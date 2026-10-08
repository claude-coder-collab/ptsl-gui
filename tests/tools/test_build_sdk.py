import json
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import build_sdk


def test_find_sdk_dir_picks_newest_version(tmp_path: Path) -> None:
    for name in ("PTSL_SDK_CPP.2025.10.0.1", "PTSL_SDK_CPP.2026.04.0.1301892", "PTSL_SDK_CPP.2026.4.0.9", "other"):
        (tmp_path / name).mkdir()
    (tmp_path / "PTSL_SDK_CPP.2099.1.0.1").touch()
    assert build_sdk.find_sdk_dir(tmp_path).name == "PTSL_SDK_CPP.2026.04.0.1301892"


def test_find_sdk_dir_raises_when_missing(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        build_sdk.find_sdk_dir(tmp_path)


def test_use_ninja_generator_replaces_xcode_only(tmp_path: Path) -> None:
    presets = tmp_path / "CMakeUserPresets.json"
    presets.write_text(
        json.dumps(
            {
                "version": 6,
                "configurePresets": [
                    {"name": "a", "inherits": ["base", "xcode"]},
                    {"name": "b", "inherits": ["base"]},
                    {"name": "c"},
                ],
                "buildPresets": [{"name": "x", "configurePreset": "a"}],
            }
        )
    )
    build_sdk.use_ninja_generator(presets)
    result = json.loads(presets.read_text())
    assert [p.get("inherits") for p in result["configurePresets"]] == [["base", "ninja"], ["base"], []]
    assert result["buildPresets"] == [{"name": "x", "configurePreset": "a"}]


def test_find_conan_tool_prefers_most_recent(tmp_path: Path) -> None:
    old = tmp_path / "p" / "b" / "protoold" / "p" / "bin" / "protoc"
    new = tmp_path / "p" / "b" / "protonew" / "p" / "bin" / "protoc"
    for path, mtime in ((old, 1_000), (new, 2_000)):
        path.parent.mkdir(parents=True)
        path.touch()
        os.utime(path, (mtime, mtime))
    assert build_sdk.find_conan_tool(tmp_path, "protoc") == new


def test_find_conan_tool_raises_when_missing(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        build_sdk.find_conan_tool(tmp_path, "protoc")


def test_parse_args_defaults() -> None:
    args = build_sdk.parse_args([])
    assert (args.config, args.with_ptslcmd) == ("Release", False)
