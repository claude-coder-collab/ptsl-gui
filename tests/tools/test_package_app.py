import plistlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import package_app

OTOOL_OUTPUT = """/tmp/PTSL GUI.app/Contents/MacOS/PTSL GUI:
\t@rpath/QtWidgets.framework/Versions/A/QtWidgets (compatibility version 6.0.0, current version 6.11.2)
\t/opt/homebrew/opt/protobuf/lib/libprotobuf.36.2.0.dylib (compatibility version 0.0.0, current version 36.2.0)
\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 2200.27.0)
\t/usr/local/lib/libfoo.dylib (compatibility version 1.0.0, current version 1.0.0)
"""

OTOOL_L_UNIVERSAL = """/x/QtCore (architecture x86_64):
Load command 9
      cmd LC_BUILD_VERSION
  cmdsize 32
 platform 1
    minos 12.0
      sdk 15.0
/x/QtCore (architecture arm64):
Load command 9
      cmd LC_BUILD_VERSION
  cmdsize 32
 platform 1
    minos 13.0
      sdk 15.0
Load command 10
      cmd LC_SOURCE_VERSION
  cmdsize 16
  version 0.0
"""

OTOOL_L_OLD_STYLE = """/x/libold.dylib:
Load command 8
      cmd LC_VERSION_MIN_MACOSX
  cmdsize 16
  version 10.13
      sdk 10.14
"""


def test_linked_libraries_parses_otool_output() -> None:
    assert package_app.linked_libraries(OTOOL_OUTPUT) == [
        "@rpath/QtWidgets.framework/Versions/A/QtWidgets",
        "/opt/homebrew/opt/protobuf/lib/libprotobuf.36.2.0.dylib",
        "/usr/lib/libc++.1.dylib",
        "/usr/local/lib/libfoo.dylib",
    ]


def test_external_references_flags_homebrew_and_usr_local() -> None:
    libraries = package_app.linked_libraries(OTOOL_OUTPUT)
    assert package_app.external_references(libraries) == [
        "/opt/homebrew/opt/protobuf/lib/libprotobuf.36.2.0.dylib",
        "/usr/local/lib/libfoo.dylib",
    ]


def test_minimum_macos_takes_highest_across_architectures() -> None:
    assert package_app.minimum_macos(OTOOL_L_UNIVERSAL) == "13.0"
    assert package_app.minimum_macos(OTOOL_L_OLD_STYLE) == "10.13"
    assert package_app.minimum_macos("/x:\nLoad command 0\n      cmd LC_SEGMENT_64\n") is None


def test_version_tuple_orders_numerically() -> None:
    assert package_app.version_tuple("13.3") > package_app.version_tuple("13.0")
    assert package_app.version_tuple("13.10") > package_app.version_tuple("13.3")
    assert package_app.version_tuple("27.0") > package_app.version_tuple("13.3")


def test_is_macho_checks_magic(tmp_path: Path) -> None:
    binary = tmp_path / "binary"
    binary.write_bytes(bytes.fromhex("cffaedfe") + b"rest")
    text = tmp_path / "text"
    text.write_text("hello")
    link = tmp_path / "link"
    link.symlink_to(binary)
    assert package_app.is_macho(binary)
    assert not package_app.is_macho(text)
    assert not package_app.is_macho(link)
    assert not package_app.is_macho(tmp_path)


def test_install_name_parses_otool_d_output() -> None:
    assert package_app.install_name("/x/libfoo.dylib:\n@rpath/libfoo.dylib\n") == "@rpath/libfoo.dylib"
    assert package_app.install_name("/x/app:\n") is None


def test_missing_rpath_libraries(tmp_path: Path) -> None:
    (tmp_path / "QtCore.framework" / "Versions" / "A").mkdir(parents=True)
    (tmp_path / "QtCore.framework" / "Versions" / "A" / "QtCore").touch()
    libraries = [
        "@rpath/QtCore.framework/Versions/A/QtCore",
        "@rpath/QtPdf.framework/Versions/A/QtPdf",
        "/usr/lib/libc++.1.dylib",
        "@executable_path/../Frameworks/x.dylib",
    ]
    assert package_app.missing_rpath_libraries(libraries, tmp_path) == ["@rpath/QtPdf.framework/Versions/A/QtPdf"]


def test_plist_minimum_macos(tmp_path: Path) -> None:
    contents = tmp_path / "App.app" / "Contents"
    contents.mkdir(parents=True)
    with (contents / "Info.plist").open("wb") as handle:
        plistlib.dump({"LSMinimumSystemVersion": "13.3"}, handle)
    assert package_app.plist_minimum_macos(tmp_path / "App.app") == "13.3"


def test_parse_args_defaults() -> None:
    args = package_app.parse_args([])
    assert args.output == package_app.PROJECT_ROOT / "dist"
    assert not args.skip_build
    assert not args.skip_tests
