"""Every entry in the serial-log allowlist must still point at a real test.

Entries are keyed on a test node id, so renaming or merging a test detaches the
one that referred to it -- silently, because nothing fails: the line it used to
permit simply starts being reported as unexpected instead. A sibling project hit
exactly this when it merged 110 tests into 29.

Node ids are gathered by reading the tree rather than by asking pytest, because
the allowlist also covers manual/ and probe/, which the default run does not
collect.
"""

import ast
from fnmatch import fnmatch
from pathlib import Path

import pytest

TESTS_ROOT = Path(__file__).resolve().parent.parent


def _known_findings(request):
    """The allowlist object itself, not a re-parse of the file it lives in."""
    for _name, plugin in request.config.pluginmanager.list_name_plugin():
        findings = getattr(plugin, "_KNOWN_SERIAL_FINDINGS", None)
        if findings is not None:
            return findings
    raise AssertionError("no loaded plugin defines _KNOWN_SERIAL_FINDINGS")


def _all_node_ids():
    ids = []
    for path in sorted(TESTS_ROOT.rglob("*.py")):
        parts = path.relative_to(TESTS_ROOT).parts
        if "build" in parts or "__pycache__" in parts or path.name == "conftest.py":
            continue
        try:
            tree = ast.parse(path.read_text(encoding="utf-8"))
        except SyntaxError:
            continue
        for node in tree.body:
            if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"):
                ids.append(f"{path.relative_to(TESTS_ROOT).as_posix()}::{node.name}")
    return ids


def test_every_allowlist_entry_matches_a_test(request):
    node_ids = _all_node_ids()
    assert node_ids, "no tests were found, so this check would pass vacuously"

    orphans = [
        rule.nodeid_pattern
        for rule in _known_findings(request)
        if not any(fnmatch(node_id, rule.nodeid_pattern) for node_id in node_ids)
    ]
    assert not orphans, (
        "these allowlist entries match no test, so the lines they permit are now "
        "reported as unexpected: " + ", ".join(orphans)
    )


def test_specific_entries_come_before_broader_ones(request):
    """Matching stops at the first hit, so order in the tuple is load-bearing.

    A broad entry placed above a specific one for the same line swallows it, and
    the specific entry's reason -- the thing that explains why that one test may
    produce it -- stops being reported.
    """
    rules = list(_known_findings(request))
    node_ids = _all_node_ids()

    for i, rule in enumerate(rules):
        covered = {n for n in node_ids if fnmatch(n, rule.nodeid_pattern)}
        for later in rules[i + 1:]:
            if later.line_pattern.pattern != rule.line_pattern.pattern:
                continue
            if later.log_name != rule.log_name:
                continue
            later_covered = {n for n in node_ids if fnmatch(n, later.nodeid_pattern)}
            if not later_covered:
                # An entry that matches nothing is the empty set, and the empty
                # set is a subset of everything -- so without this it reads as
                # shadowed by whatever comes before it and fails here with a
                # message about ordering. That entry is the other test's business.
                continue
            assert not later_covered <= covered, (
                f"{later.nodeid_pattern!r} is fully shadowed by the earlier "
                f"{rule.nodeid_pattern!r}; put the specific entry first"
            )
