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


def test_package_folders_reads_cmakedeps_data(tmp_path: Path) -> None:
    (tmp_path / "protobuf-release-armv8-data.cmake").write_text(
        'set(protobuf_PACKAGE_FOLDER_RELEASE "/c/p/b/proto1/p")\nset(protobuf_INCLUDE_DIRS_RELEASE "x")\n'
    )
    (tmp_path / "gRPC-release-armv8-data.cmake").write_text('set(grpc_PACKAGE_FOLDER_RELEASE "/c/p/b/grpc1/p")\n')
    (tmp_path / "unrelated.cmake").write_text('set(zlib_PACKAGE_FOLDER_RELEASE "/nope")\n')
    assert build_sdk.package_folders(tmp_path) == {"protobuf": Path("/c/p/b/proto1/p"), "grpc": Path("/c/p/b/grpc1/p")}
    assert build_sdk.package_folders(tmp_path / "missing") == {}


def test_ensure_conan_conf_adds_missing_lines_once(tmp_path: Path) -> None:
    home = tmp_path / "conan"
    build_sdk.ensure_conan_conf(home, ("a=1", "b=2"))
    assert (home / "global.conf").read_text() == "a=1\nb=2\n"
    (home / "global.conf").write_text("keep=0\na=1\n")
    build_sdk.ensure_conan_conf(home, ("a=1", "b=2"))
    build_sdk.ensure_conan_conf(home, ("a=1", "b=2"))
    assert (home / "global.conf").read_text() == "keep=0\na=1\nb=2\n"


def test_parse_args_defaults() -> None:
    args = build_sdk.parse_args([])
    assert (args.config, args.with_ptslcmd, args.os_version) == ("Release", False, "13.3")
