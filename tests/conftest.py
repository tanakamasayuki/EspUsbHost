import re
from pathlib import Path

import pytest


_SERIAL_ERROR_PATTERNS = (
    re.compile(r"(?:^|\s)E \(\d+\)"),
    re.compile(r"ESP_ERR_(?!OK\b)"),
    re.compile(r"ESP_ERROR_CHECK failed", re.IGNORECASE),
    re.compile(r"Guru Meditation"),
    re.compile(r"assert failed", re.IGNORECASE),
    re.compile(r"abort\(\)"),
    re.compile(r"Backtrace:"),
    re.compile(r"(?:task |interrupt )?watchdog", re.IGNORECASE),
    re.compile(r"Stack canary watchpoint triggered", re.IGNORECASE),
    re.compile(r"CORRUPT HEAP", re.IGNORECASE),
    re.compile(r"Brownout detector was triggered", re.IGNORECASE),
)
_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
_AUDIT_RESULTS_KEY = pytest.StashKey[list[tuple[str, str, list[str]]]]()
_AUDIT_SECTION_KEY = pytest.StashKey[str]()
_AUDIT_LOG_COUNT_KEY = pytest.StashKey[int]()
_AUDIT_ROOTS_KEY = pytest.StashKey[set[str]]()


def _serial_error_lines(log_path: Path) -> list[str]:
    findings = []
    text = log_path.read_text(encoding="utf-8", errors="replace")
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = _ANSI_ESCAPE_RE.sub("", raw_line)
        if any(pattern.search(line) for pattern in _SERIAL_ERROR_PATTERNS):
            findings.append(f"{log_path.name}:{line_number}: {line}")
    return findings


@pytest.fixture(autouse=True)
def serial_log_audit(request, test_case_tempdir):
    """Collect suspicious DUT and peer serial output without failing the test."""
    yield

    log_dir = Path(test_case_tempdir)
    log_paths = sorted(log_dir.glob("*.log"))
    request.config.stash[_AUDIT_LOG_COUNT_KEY] += len(log_paths)
    if log_paths:
        request.config.stash[_AUDIT_ROOTS_KEY].add(str(log_dir.parent))

    findings = []
    for log_path in log_paths:
        findings.extend(_serial_error_lines(log_path))

    if not findings:
        return

    section = f"Log directory: {log_dir}\n" + "\n".join(findings)
    request.node.stash[_AUDIT_SECTION_KEY] = section
    request.config.stash[_AUDIT_RESULTS_KEY].append(
        (request.node.nodeid, str(log_dir), findings)
    )


def pytest_configure(config):
    config.stash[_AUDIT_RESULTS_KEY] = []
    config.stash[_AUDIT_LOG_COUNT_KEY] = 0
    config.stash[_AUDIT_ROOTS_KEY] = set()


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when != "teardown":
        return

    section = item.stash.get(_AUDIT_SECTION_KEY, "")
    if section:
        # pytest-html appends teardown sections to the test's expandable log.
        report.sections.append(("serial error audit teardown", section))


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    results = config.stash[_AUDIT_RESULTS_KEY]
    log_count = config.stash[_AUDIT_LOG_COUNT_KEY]
    log_roots = sorted(config.stash[_AUDIT_ROOTS_KEY])
    terminalreporter.section("serial log audit", sep="=")
    for log_root in log_roots:
        terminalreporter.write_line(f"Log root: {log_root}")
    if not results:
        terminalreporter.write_line(
            f"No suspicious serial output found in {log_count} DUT/peer log(s)."
        )
        return

    finding_count = sum(len(findings) for _, _, findings in results)
    terminalreporter.write_line(
        f"Found {finding_count} suspicious line(s) in {len(results)} test(s); tests were not failed."
    )
    for nodeid, log_dir, findings in results:
        terminalreporter.write_line(nodeid)
        terminalreporter.write_line(f"  Log directory: {log_dir}")
        for finding in findings:
            terminalreporter.write_line(f"  {finding}")
