import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import gen_catalog

FIXTURE = ROOT / "tests" / "fixtures" / "proto" / "fixture.proto"
SDK_PROTOS = sorted(ROOT.glob("PTSL_SDK_CPP.*/Source/PTSL.proto"))


@pytest.fixture(scope="module")
def commands() -> dict[str, gen_catalog.Command]:
    return {command.name: command for command in gen_catalog.build_catalog(FIXTURE.read_text())}


@pytest.mark.parametrize(
    ("enum_name", "expected"),
    [
        ("CId_GetTrackList", "Get Track List"),
        ("CId_GetPTSLVersion", "Get PTSL Version"),
        ("CId_ExportSelectedTracksAsAAFOMF", "Export Selected Tracks As AAFOMF"),
        ("CId_GetSessionIDs", "Get Session IDs"),
        ("CId_SetTrackDSPModeSafeState", "Set Track DSP Mode Safe State"),
        ("CId_Undo", "Undo"),
        ("Plain", "Plain"),
    ],
)
def test_display_name(enum_name: str, expected: str) -> None:
    assert gen_catalog.display_name(enum_name) == expected


def test_only_cid_entries_become_commands(commands: dict[str, gen_catalog.Command]) -> None:
    assert list(commands) == ["CId_MakeWidget", "CId_ListWidgets", "CId_Ping", "CId_DeleteWidget", "CId_GetPTSLVersion"]
    assert [command.id for command in commands.values()] == [0, 1, 2, 3, 4]


def test_aliases_attach_to_command(commands: dict[str, gen_catalog.Command]) -> None:
    assert commands["CId_MakeWidget"].deprecated_aliases == ["MakeWidget"]
    assert commands["CId_DeleteWidget"].deprecated_aliases == ["OldDeleteWidget", "AncientDeleteWidget"]
    assert commands["CId_MakeWidget"].deprecated is None


def test_alias_comment_does_not_leak_into_command(commands: dict[str, gen_catalog.Command]) -> None:
    assert commands["CId_MakeWidget"].description == "Makes a new widget. Spans two lines."


def test_types_categories_and_since(commands: dict[str, gen_catalog.Command]) -> None:
    make = commands["CId_MakeWidget"]
    assert (make.request_type, make.response_type) == ("MakeWidgetRequestBody", "MakeWidgetResponseBody")
    assert make.categories == ["widgets"]
    assert make.since == "2023.3"
    assert make.inferred_types == []
    assert commands["CId_ListWidgets"].categories == ["widgets", "queries"]
    assert commands["CId_ListWidgets"].since == "2024.1.2"
    assert commands["CId_Ping"].since is None


def test_examples_with_captions_comments_and_repair(commands: dict[str, gen_catalog.Command]) -> None:
    first, second = commands["CId_MakeWidget"].request_examples
    assert first.caption == "Simple widget."
    assert json.loads(first.text) == {"name": "w1", "size": 3}
    assert (first.valid, first.repaired) == (True, False)
    assert second.caption == "A coloured widget, with a caption on two lines."
    assert second.comments == ["trailing commas are tolerated"]
    assert (second.valid, second.repaired) == (True, True)
    assert json.loads(second.text) == {"name": "w2", "colour": "WColour_Red"}
    (response,) = commands["CId_MakeWidget"].response_examples
    assert response.repaired and json.loads(response.text) == {"widget_id": "abc", "created": True}


def test_unrepairable_example_is_flagged(commands: dict[str, gen_catalog.Command]) -> None:
    (example,) = commands["CId_DeleteWidget"].request_examples
    assert not example.valid
    assert "..." in example.text


def test_brief_details_lists_notes_and_todo(commands: dict[str, gen_catalog.Command]) -> None:
    listing = commands["CId_ListWidgets"]
    assert listing.description == "Lists widgets.\n\nReturns all widgets. Kinds:\n\n- Round\n  - has no corners\n- Square"
    assert listing.notes == ["Results are paged. Use the pagination fields.", "See also: MakeWidget"]
    assert "TODO" not in listing.description + "".join(listing.notes)


def test_type_fallback_by_convention(commands: dict[str, gen_catalog.Command]) -> None:
    delete = commands["CId_DeleteWidget"]
    assert delete.request_type == "DeleteWidgetRequestBody"
    assert delete.inferred_types == ["request_type"]
    listing = commands["CId_ListWidgets"]
    assert (listing.request_type, listing.response_type) == ("ListWidgetsRequestBody", "ListWidgetsResponseBody")
    assert listing.inferred_types == ["request_type", "response_type"]
    assert commands["CId_Ping"].request_type is None


def test_deprecated_command(commands: dict[str, gen_catalog.Command]) -> None:
    assert commands["CId_DeleteWidget"].deprecated == "since Widget Host 2025.1, use CId_MakeWidget with size 0"


def test_validate_reports_problems() -> None:
    text = FIXTURE.read_text()
    problems = gen_catalog.validate(gen_catalog.build_catalog(text), text)
    assert "CId_DeleteWidget: request_examples[1] is not valid JSON" in problems
    assert "CId_Ping: no @since" in problems
    assert not any("MakeWidget" in problem and "type" in problem for problem in problems)


def test_missing_enum_raises() -> None:
    with pytest.raises(ValueError):
        gen_catalog.build_catalog('syntax = "proto3";\nmessage A {}\n')


def test_repair_json_leaves_valid_json_alone() -> None:
    text = '{\n  "a": [1, 2],\n  "b": {"c": "x,}"}\n}'
    assert gen_catalog.repair_json(text) == text


def test_main_writes_document(tmp_path: Path) -> None:
    output = tmp_path / "out" / "catalog.json"
    assert gen_catalog.main(["--proto", str(FIXTURE), "--output", str(output), "--quiet"]) == 0
    document = json.loads(output.read_text())
    assert document["schema_version"] == gen_catalog.SCHEMA_VERSION
    assert document["source"]["proto"] == "fixture.proto"
    assert len(document["source"]["sha256"]) == 64
    assert document["categories"] == ["editing", "queries", "utility", "widgets"]
    assert document["commands"][0]["request_examples"][0]["caption"] == "Simple widget."


def test_main_does_not_rewrite_unchanged_output(tmp_path: Path) -> None:
    output = tmp_path / "catalog.json"
    gen_catalog.main(["--proto", str(FIXTURE), "--output", str(output), "--quiet"])
    output.touch()
    before = output.stat().st_mtime_ns
    gen_catalog.main(["--proto", str(FIXTURE), "--output", str(output), "--quiet"])
    assert output.stat().st_mtime_ns == before


def test_main_strict_fails_on_problems(tmp_path: Path) -> None:
    assert gen_catalog.main(["--proto", str(FIXTURE), "--output", str(tmp_path / "c.json"), "--strict", "--quiet"]) == 1


@pytest.mark.skipif(not SDK_PROTOS, reason="PTSL SDK not present")
def test_real_sdk_proto() -> None:
    text = SDK_PROTOS[-1].read_text()
    commands = gen_catalog.build_catalog(text)
    assert len(commands) > 100
    assert len({command.id for command in commands}) == len(commands)
    assert all(command.name.startswith("CId_") for command in commands)
    assert all(command.description for command in commands)
    problems = gen_catalog.validate(commands, text)
    assert not any("is not a message" in problem for problem in problems)
    examples = [example for command in commands for example in command.request_examples]
    assert sum(example.valid for example in examples) / len(examples) > 0.95
