#!/usr/bin/env python3
"""Generate the PTSL GUI command catalog from the doc comments of the CommandId enum in PTSL.proto."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path

SCHEMA_VERSION = 1

_ENUM_RE = re.compile(r"\benum\s+CommandId\s*\{")
_MESSAGE_RE = re.compile(r"^\s*message\s+(\w+)", re.MULTILINE)
_ENTRY_RE = re.compile(r"\s*(\w+)\s*=\s*(-?\d+)\s*(?:\[[^\]]*\])?\s*;")
_VERSION_RE = re.compile(r"\d{4}\.\d+(?:\.\d+)?")
_WORD_RE = re.compile(r"[A-Z]{2,}s(?![a-z])|[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+")

_TYPE_TAGS = {"request_body_type": "request_type", "response_body_type": "response_type"}
_EXAMPLE_TAGS = {"request_body_json_example": "request_examples", "response_body_json_example": "response_examples"}
_PARAGRAPH_TAGS = {"brief", "details", "note", "sa"}
_TRAILING_COMMA_RE = re.compile(r",(\s*[}\]])")
_MISSING_COMMA_RE = re.compile(r"""("|\d|true|false|null|[}\]])([ \t]*\n\s*")""")
_LIST_ITEM_RE = re.compile(r"^-#?\s+")
_IGNORED_TAGS = {"cond", "endcond"}


@dataclass
class Example:
    text: str
    caption: str = ""
    comments: list[str] = field(default_factory=list)
    valid: bool = True
    repaired: bool = False


@dataclass
class Command:
    id: int
    name: str
    display_name: str
    description: str = ""
    notes: list[str] = field(default_factory=list)
    categories: list[str] = field(default_factory=list)
    request_type: str | None = None
    response_type: str | None = None
    request_examples: list[Example] = field(default_factory=list)
    response_examples: list[Example] = field(default_factory=list)
    since: str | None = None
    deprecated: str | None = None
    deprecated_aliases: list[str] = field(default_factory=list)
    inferred_types: list[str] = field(default_factory=list)


def display_name(enum_name: str) -> str:
    base = enum_name.removeprefix("CId_")
    return " ".join(_WORD_RE.findall(base)) or base


def extract_enum_body(proto_text: str) -> str:
    match = _ENUM_RE.search(proto_text)
    if not match:
        raise ValueError("enum CommandId not found")
    depth = 1
    for index in range(match.end(), len(proto_text)):
        if proto_text[index] == "{":
            depth += 1
        elif proto_text[index] == "}":
            depth -= 1
            if depth == 0:
                return proto_text[match.end() : index]
    raise ValueError("enum CommandId is not terminated")


def iter_entries(enum_body: str) -> list[tuple[str | None, str, int]]:
    """Return (doc comment or None, name, value) for each enum value, skipping options and reserved statements."""
    entries: list[tuple[str | None, str, int]] = []
    pending_doc: str | None = None
    pos = 0
    while pos < len(enum_body):
        if enum_body.startswith("/**", pos):
            end = enum_body.index("*/", pos + 3)
            pending_doc = enum_body[pos + 3 : end]
            pos = end + 2
        elif enum_body.startswith("/*", pos):
            pos = enum_body.index("*/", pos + 2) + 2
        elif enum_body.startswith("//", pos):
            newline = enum_body.find("\n", pos)
            pos = len(enum_body) if newline == -1 else newline + 1
        elif enum_body[pos].isspace():
            pos += 1
        else:
            semicolon = enum_body.index(";", pos)
            statement = enum_body[pos : semicolon + 1]
            entry = _ENTRY_RE.fullmatch(statement)
            if entry and entry.group(1) not in ("option", "reserved"):
                entries.append((pending_doc, entry.group(1), int(entry.group(2))))
            pending_doc = None
            pos = semicolon + 1
    return entries


def clean_comment_lines(doc: str) -> list[str]:
    lines = []
    for raw in doc.splitlines():
        line = raw.strip()
        if line.startswith("*"):
            line = line[1:]
            if line.startswith(" "):
                line = line[1:]
        lines.append(line.rstrip())
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return lines


def _parse_example(lines: list[str], caption: str) -> Example:
    body: list[str] = []
    comments: list[str] = []
    for line in lines:
        if line.strip().startswith("#"):
            comment = line.strip().lstrip("#").strip()
            if comment:
                comments.append(comment)
        else:
            body.append(line.replace("\t", "  "))
    text = "\n".join(body).strip()
    if _is_json(text):
        return Example(text=text, caption=caption, comments=comments)
    repaired = repair_json(text)
    if _is_json(repaired):
        return Example(text=repaired, caption=caption, comments=comments, repaired=True)
    return Example(text=text, caption=caption, comments=comments, valid=False)


def _is_json(text: str) -> bool:
    try:
        json.loads(text)
    except json.JSONDecodeError:
        return False
    return True


def repair_json(text: str) -> str:
    """Fix the common documentation typos: trailing commas and missing commas between lines."""
    if _is_json(text):
        return text
    text = _TRAILING_COMMA_RE.sub(r"\1", text)
    return _MISSING_COMMA_RE.sub(r"\1,\2", text)


def _join_paragraphs(lines: list[str]) -> str:
    paragraphs: list[str] = []
    current: list[str] = []
    for line in lines:
        stripped = line.strip()
        if not stripped:
            if current:
                paragraphs.append("\n".join(current))
                current = []
        elif _LIST_ITEM_RE.match(stripped):
            indent = "  " if stripped.startswith("-#") else ""
            current.append(indent + _LIST_ITEM_RE.sub("- ", stripped))
        elif current:
            current[-1] = f"{current[-1]} {stripped}"
        else:
            current.append(stripped)
    if current:
        paragraphs.append("\n".join(current))
    return "\n\n".join(paragraphs)


def apply_doc(command: Command, doc: str) -> None:
    lines = clean_comment_lines(doc)
    description: list[str] = []
    pending: list[str] = []
    seen_structured = False
    index = 0

    def free_text_target() -> list[str]:
        return pending if seen_structured else description

    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if not stripped.startswith("@"):
            if not stripped.startswith("TODO"):
                free_text_target().append(line)
            index += 1
            continue

        tag, _, rest = stripped[1:].partition(" ")
        rest = rest.strip()
        index += 1

        if tag in _EXAMPLE_TAGS:
            seen_structured = True
            body: list[str] = []
            while index < len(lines) and lines[index].strip() != "@end_example":
                body.append(lines[index])
                index += 1
            index += 1
            caption = _join_paragraphs(pending)
            pending.clear()
            getattr(command, _EXAMPLE_TAGS[tag]).append(_parse_example(body, caption))
        elif tag in _TYPE_TAGS:
            seen_structured = True
            setattr(command, _TYPE_TAGS[tag], rest.split()[0] if rest else None)
        elif tag.startswith("category_"):
            seen_structured = True
            category = tag.removeprefix("category_")
            if category != "all" and category not in command.categories:
                command.categories.append(category)
        elif tag == "since":
            seen_structured = True
            version = _VERSION_RE.search(rest)
            command.since = version.group(0) if version else rest or None
        elif tag == "deprecated":
            command.deprecated = rest or "deprecated"
        elif tag in _PARAGRAPH_TAGS:
            paragraph = [rest]
            while index < len(lines) and lines[index].strip() and not lines[index].strip().startswith("@"):
                paragraph.append(lines[index])
                index += 1
            if tag == "note":
                command.notes.append(_join_paragraphs(paragraph))
            elif tag == "sa":
                command.notes.append(f"See also: {_join_paragraphs(paragraph)}")
            else:
                free_text_target().extend([*paragraph, ""])
        elif tag in _IGNORED_TAGS:
            pass
        else:
            free_text_target().append(stripped)

    command.description = _join_paragraphs(description)
    leftover = _join_paragraphs(pending)
    if leftover:
        command.notes.append(leftover)


def resolve_types(command: Command, messages: set[str]) -> None:
    """Fall back to the <Name>RequestBody / <Name>ResponseBody convention when the doc type is absent or unknown."""
    base = command.name.removeprefix("CId_")
    for attribute, suffix in (("request_type", "RequestBody"), ("response_type", "ResponseBody")):
        declared = getattr(command, attribute)
        conventional = base + suffix
        if (declared is None or declared not in messages) and conventional in messages:
            setattr(command, attribute, conventional)
            command.inferred_types.append(attribute)


def build_catalog(proto_text: str) -> list[Command]:
    messages = set(_MESSAGE_RE.findall(proto_text))
    entries = iter_entries(extract_enum_body(proto_text))
    commands: dict[int, Command] = {}
    aliases: dict[int, list[str]] = {}
    for doc, name, value in entries:
        if value < 0:
            continue
        if name.startswith("CId_"):
            command = Command(id=value, name=name, display_name=display_name(name))
            if doc:
                apply_doc(command, doc)
            resolve_types(command, messages)
            commands[value] = command
        else:
            aliases.setdefault(value, []).append(name)
    for value, names in aliases.items():
        if value in commands:
            commands[value].deprecated_aliases.extend(names)
    return sorted(commands.values(), key=lambda command: command.id)


def validate(commands: list[Command], proto_text: str) -> list[str]:
    messages = set(_MESSAGE_RE.findall(proto_text))
    problems: list[str] = []
    seen_names: set[str] = set()
    for command in commands:
        if command.name in seen_names:
            problems.append(f"{command.name}: duplicate name")
        seen_names.add(command.name)
        for kind in ("request_type", "response_type"):
            type_name = getattr(command, kind)
            if type_name and type_name not in messages:
                problems.append(f"{command.name}: {kind} {type_name} is not a message in the proto")
        for kind in ("request_examples", "response_examples"):
            for number, example in enumerate(getattr(command, kind), 1):
                if not example.valid:
                    problems.append(f"{command.name}: {kind}[{number}] is not valid JSON")
        if not command.since:
            problems.append(f"{command.name}: no @since")
    return problems


def catalog_document(commands: list[Command], proto_path: Path, proto_bytes: bytes) -> dict:
    categories: list[str] = []
    for command in commands:
        for category in command.categories:
            if category not in categories:
                categories.append(category)
    return {
        "schema_version": SCHEMA_VERSION,
        "source": {"proto": proto_path.name, "sha256": hashlib.sha256(proto_bytes).hexdigest()},
        "categories": sorted(categories),
        "commands": [asdict(command) for command in commands],
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--proto", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--strict", action="store_true", help="Fail if validation finds problems")
    parser.add_argument("--quiet", action="store_true", help="Do not print validation problems")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    proto_bytes = args.proto.read_bytes()
    proto_text = proto_bytes.decode("utf-8")
    commands = build_catalog(proto_text)
    problems = validate(commands, proto_text)
    if not args.quiet:
        for problem in problems:
            print(f"warning: {problem}", file=sys.stderr)
    if args.strict and problems:
        return 1
    document = catalog_document(commands, args.proto, proto_bytes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    new_text = json.dumps(document, indent=1, ensure_ascii=False) + "\n"
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != new_text:
        args.output.write_text(new_text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
