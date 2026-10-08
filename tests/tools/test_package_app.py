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


def test_parse_args_defaults() -> None:
    args = package_app.parse_args([])
    assert args.output == package_app.PROJECT_ROOT / "dist"
    assert not args.skip_build


def test_install_name_parses_otool_d_output() -> None:
    assert package_app.install_name("/x/libfoo.dylib:\n@rpath/libfoo.dylib\n") == "@rpath/libfoo.dylib"
    assert package_app.install_name("/x/app:\n") is None


def test_qt_library_paths_lists_qt_kegs(tmp_path: Path) -> None:
    for name in ("qtbase", "qtsvg", "protobuf"):
        (tmp_path / name / "lib").mkdir(parents=True)
    (tmp_path / "qtnolib").mkdir()
    assert package_app.qt_library_paths(tmp_path) == [tmp_path / "qtbase" / "lib", tmp_path / "qtsvg" / "lib"]


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
