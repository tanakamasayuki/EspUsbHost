#!/usr/bin/env python3
"""Increment the library version in library.properties and print the new value."""

from __future__ import annotations

import argparse
import pathlib
import re


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--level",
        choices=("major", "minor", "patch"),
        default="patch",
        help="Version component to increment (default: patch).",
    )
    parser.add_argument(
        "--preview",
        action="store_true",
        help="Print the next version without modifying the file.",
    )
    return parser.parse_args()


def load_version(props_path: pathlib.Path) -> tuple[int, int, int]:
    content = props_path.read_text(encoding="utf-8")
    match = re.search(r"^version=(\d+)\.(\d+)\.(\d+)$", content, flags=re.MULTILINE)
    if not match:
        raise ValueError("version property not found in library.properties")
    return tuple(int(part) for part in match.groups())


def load_library_name(props_path: pathlib.Path) -> str:
    content = props_path.read_text(encoding="utf-8")
    match = re.search(r"^name=(.+)$", content, flags=re.MULTILINE)
    if not match:
        raise ValueError("name property not found in library.properties")
    return match.group(1).strip()


def format_version(version: tuple[int, int, int]) -> str:
    return ".".join(str(part) for part in version)


def bump(version: tuple[int, int, int], level: str) -> tuple[int, int, int]:
    major, minor, patch = version
    if level == "major":
        return major + 1, 0, 0
    if level == "minor":
        return major, minor + 1, 0
    return major, minor, patch + 1


def update_file(props_path: pathlib.Path, new_version: str) -> None:
    content = props_path.read_text(encoding="utf-8")
    updated = re.sub(
        r"^version=.*$",
        f"version={new_version}",
        content,
        flags=re.MULTILINE,
    )
    props_path.write_text(updated, encoding="utf-8")


def update_sketch_files(
    examples_root: pathlib.Path,
    library_name: str,
    new_version: str,
) -> None:
    version_pattern = re.compile(
        rf"(^\s*-\s+{re.escape(library_name)}\s*\()\d+\.\d+\.\d+(\)\s*)",
        flags=re.MULTILINE,
    )
    dir_pattern = re.compile(
        r"^(?P<indent>\s*-\s+)dir\s*:\s*.+$",
        flags=re.MULTILINE,
    )
    for sketch in examples_root.rglob("sketch.yaml"):
        content = sketch.read_text(encoding="utf-8")
        updated = version_pattern.sub(
            lambda match: f"{match.group(1)}{new_version}{match.group(2)}",
            content,
        )
        updated = dir_pattern.sub(
            lambda match: f"{match.group('indent')}{library_name} ({new_version})",
            updated,
        )
        if updated != content:
            sketch.write_text(updated, encoding="utf-8")



def update_changelog(changelog_path: pathlib.Path, new_version: str) -> None:
    """Move Unreleased entries into a new version section."""
    if not changelog_path.exists():
        return

    content = changelog_path.read_text(encoding="utf-8")
    lines = content.splitlines()

    try:
        unreleased_idx = next(
            idx for idx, line in enumerate(lines) if line.strip() == "## Unreleased"
        )
    except StopIteration:
        return

    def is_header(idx: int) -> bool:
        return lines[idx].startswith("## ")

    next_header_idx = next(
        (idx for idx in range(unreleased_idx + 1, len(lines)) if is_header(idx)),
        len(lines),
    )

    entry_start = unreleased_idx + 1
    entry_end = next_header_idx
    while entry_start < entry_end and not lines[entry_start].strip():
        entry_start += 1
    while entry_end > entry_start and not lines[entry_end - 1].strip():
        entry_end -= 1

    entries = lines[entry_start:entry_end]
    if not entries:
        return

    new_lines: list[str] = []
    new_lines.extend(lines[:unreleased_idx + 1])
    new_lines.append("")  # blank line after Unreleased
    new_lines.append(f"## {new_version}")
    new_lines.extend(entries)

    tail = lines[next_header_idx:]
    if tail and tail[0].strip():
        new_lines.append("")
    new_lines.extend(tail)

    changelog_path.write_text("\n".join(new_lines) + "\n", encoding="utf-8")


def _sanitize_for_macro(name: str) -> str:
    """Return an uppercase identifier-safe version of name for macro prefixes.

    Non-alphanumeric characters are replaced with underscores. If the result
    starts with a digit, prefix with an underscore.
    """
    s = re.sub(r"[^A-Za-z0-9]+", "_", name).upper()
    if re.match(r"^[0-9]", s):
        s = "_" + s
    return s


def _sanitize_for_filename(name: str) -> str:
    """Return a lowercase filename-safe version of name."""
    s = re.sub(r"[^A-Za-z0-9]+", "_", name).lower()
    return s


def write_version_header_for_library(library_name: str, version: tuple[int, int, int]) -> pathlib.Path:
    """Write a library-specific version header and return its path.

    The header filename will be `src/<sanitized_name>_version.h` where
    <sanitized_name> is the lowercase, underscore-separated library name.
    The macros inside will be prefixed with the uppercase library name
    to reduce collisions (e.g. MYLIB_VERSION_MAJOR).
    """
    macro_prefix = _sanitize_for_macro(library_name)
    file_base = f"{_sanitize_for_filename(library_name)}_version.h"
    header_path = pathlib.Path("src") / file_base

    major, minor, patch = version
    version_str = f"{major}.{minor}.{patch}"
    guard = f"{macro_prefix}_VERSION_H"
    content = (
        "/* Auto-generated by tools/bump_version.py - do not edit manually */\n"
        f"#ifndef {guard}\n"
        f"#define {guard}\n\n"
        f"#define {macro_prefix}_VERSION_MAJOR {major}\n"
        f"#define {macro_prefix}_VERSION_MINOR {minor}\n"
        f"#define {macro_prefix}_VERSION_PATCH {patch}\n"
        f"#define {macro_prefix}_VERSION_STR \"{version_str}\"\n\n"
        f"#endif // {guard}\n"
    )
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text(content, encoding="utf-8")
    return header_path


def main() -> None:
    args = parse_args()
    props_path = pathlib.Path("library.properties")
    current = load_version(props_path)
    target = bump(current, args.level)
    next_version = format_version(target)
    if not args.preview:
        library_name = load_library_name(props_path)
        update_file(props_path, next_version)
        update_changelog(pathlib.Path("CHANGELOG.md"), next_version)
        examples_root = pathlib.Path("examples")
        if examples_root.exists():
            update_sketch_files(examples_root, library_name, next_version)
        # Write a C header with the version so the firmware/source can include it.
        header_path = write_version_header_for_library(library_name, target)
        header_path_str = str(header_path)
    else:
        # preview: no header written, but keep second-line output empty for workflow parsing
        header_path_str = ""

    # Output two lines: first the version, second the header path (or empty)
    print(next_version)
    print(header_path_str)


if __name__ == "__main__":
    main()
