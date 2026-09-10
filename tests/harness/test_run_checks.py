"""Tests for the check runner in ``tests/conftest.py``.

The reverse audit is only worth running if it actually reverses. If the reversal
broke, ``ESPUSBHOST_REVERSE_CHECKS=1`` would quietly become a second forward run:
every module would keep passing and the audit would report success while checking
nothing. That is the same defect this suite went looking for in its own
assertions -- one satisfied by circumstance rather than by the code under test --
so the runner gets the same treatment. These show the order actually changes,
not merely that a reversed run passes.

No board, no serial port, and no sketch in this directory, so nothing is built
for it.
"""

import pytest


def test_checks_run_in_the_order_given(run_checks, monkeypatch):
    monkeypatch.delenv("ESPUSBHOST_REVERSE_CHECKS", raising=False)
    seen = []

    def first():
        seen.append("first")

    def second():
        seen.append("second")

    run_checks([first, second])
    assert seen == ["first", "second"]


def test_the_env_var_reverses_them(run_checks, monkeypatch):
    monkeypatch.setenv("ESPUSBHOST_REVERSE_CHECKS", "1")
    seen = []

    def first():
        seen.append("first")

    def second():
        seen.append("second")

    run_checks([first, second])
    assert seen == ["second", "first"], "the reverse audit is not reversing"


def test_only_the_exact_value_reverses(run_checks, monkeypatch):
    # Set to something else and the run must stay forward, so a stray value in a
    # shell profile cannot silently turn every run into a reversed one.
    monkeypatch.setenv("ESPUSBHOST_REVERSE_CHECKS", "0")
    seen = []

    def first():
        seen.append("first")

    def second():
        seen.append("second")

    run_checks([first, second])
    assert seen == ["first", "second"]


def test_a_failing_check_stops_the_run_and_names_itself(run_checks, monkeypatch):
    """Failures propagate rather than being collected.

    Collecting them was tried and removed: it replaced pytest's traceback with a
    one-line summary, and on hardware a check that fails leaves the board in a
    state the next one misreads. Both properties are asserted here so a future
    change back to collecting fails loudly rather than quietly costing the
    traceback.
    """
    monkeypatch.delenv("ESPUSBHOST_REVERSE_CHECKS", raising=False)
    seen = []

    def failing_check():
        seen.append("failing_check")
        raise AssertionError("boom")

    def never_reached():
        seen.append("never_reached")

    with pytest.raises(AssertionError, match="boom") as excinfo:
        run_checks([failing_check, never_reached])

    assert seen == ["failing_check"], "a failure must stop the run, not be collected"
    # The frame the traceback points at is the check itself, which is what makes
    # one test per module as informative as several tests were.
    assert excinfo.traceback[-1].frame.code.name == "failing_check"
