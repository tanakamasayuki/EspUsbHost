import re
from dataclasses import dataclass
from fnmatch import fnmatch
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

@dataclass(frozen=True)
class _KnownSerialFinding:
    nodeid_pattern: str
    log_name: str
    line_pattern: re.Pattern[str]
    max_count: int
    reason: str


# Lines that a healthy run can legitimately produce. Each entry is pinned to the
# test and log it was observed in and capped at max_count, so the same message
# appearing somewhere new, or more often than expected, is still reported.
_KNOWN_SERIAL_FINDINGS = (
    _KnownSerialFinding(
        nodeid_pattern="*peer/hid_mouse/test_hid_mouse.py::test_hid_mouse_move",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="transient disconnect while peer firmware is replaced",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/hid_consumer_control/test_hid_consumer_control.py::test_hid_consumer_control_volume",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="transient disconnect while peer firmware is replaced",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/hid_keyboard_nkro/test_hid_keyboard_nkro.py::test_hid_keyboard_nkro_detected",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="transient disconnect while peer firmware is replaced",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/hid_system_control/test_hid_system_control.py::test_hid_system_control",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="transient disconnect while peer firmware is replaced",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/usb_audio/test_usb_audio.py::test_usb_audio_bidirectional",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="transient disconnect while peer firmware is replaced",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/usb_midi/test_usb_midi.py::test_usb_midi_lifecycle_listeners_on_peer_reboot",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="the test reboots the peer on purpose to produce a disconnect, so an in-flight transfer can race it",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*printer*",
        log_name="dut.log",
        line_pattern=re.compile(r"ENUM: Device returned less bytes than requested$"),
        max_count=2,
        reason="the XP-C58K declares product and serial string descriptors it then returns short; enumeration continues and the printer works",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*probe/printer_class/*",
        log_name="dut.log",
        line_pattern=re.compile(r"USBH: Dev \d+ EP 0 STALL$"),
        max_count=3,
        reason="the probe deliberately sends GET_DEVICE_ID forms the printer rejects, to find which addressing it accepts",
    ),
)
_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
_AUDIT_RESULTS_KEY = pytest.StashKey[
    list[tuple[str, str, list[str], list[tuple[str, str]]]]
]()
_AUDIT_SECTION_KEY = pytest.StashKey[str]()
_AUDIT_LOG_COUNT_KEY = pytest.StashKey[int]()
_AUDIT_ROOTS_KEY = pytest.StashKey[set[str]]()


def _serial_error_lines(
    nodeid: str, log_path: Path
) -> tuple[list[str], list[tuple[str, str]]]:
    unexpected = []
    known = []
    known_counts = [0] * len(_KNOWN_SERIAL_FINDINGS)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = _ANSI_ESCAPE_RE.sub("", raw_line)
        if not any(pattern.search(line) for pattern in _SERIAL_ERROR_PATTERNS):
            continue

        finding = f"{log_path.name}:{line_number}: {line}"
        matched_rule = None
        for index, rule in enumerate(_KNOWN_SERIAL_FINDINGS):
            if (
                known_counts[index] < rule.max_count
                and fnmatch(nodeid, rule.nodeid_pattern)
                and log_path.name == rule.log_name
                and rule.line_pattern.search(line)
            ):
                known_counts[index] += 1
                matched_rule = rule
                break

        if matched_rule:
            known.append((finding, matched_rule.reason))
        else:
            unexpected.append(finding)

    return unexpected, known


@pytest.fixture(autouse=True)
def serial_log_audit(request, test_case_tempdir):
    """Collect suspicious DUT and peer serial output without failing the test."""
    yield

    log_dir = Path(test_case_tempdir)
    log_paths = sorted(log_dir.glob("*.log"))
    request.config.stash[_AUDIT_LOG_COUNT_KEY] += len(log_paths)
    if log_paths:
        request.config.stash[_AUDIT_ROOTS_KEY].add(str(log_dir.parent))

    unexpected = []
    known = []
    for log_path in log_paths:
        log_unexpected, log_known = _serial_error_lines(request.node.nodeid, log_path)
        unexpected.extend(log_unexpected)
        known.extend(log_known)

    if not unexpected and not known:
        return

    section_lines = [f"Log directory: {log_dir}"]
    if unexpected:
        section_lines.append("Unexpected suspicious lines:")
        section_lines.extend(unexpected)
    if known:
        section_lines.append("Known allowed lines:")
        section_lines.extend(
            f"{finding} [reason: {reason}]" for finding, reason in known
        )
    section = "\n".join(section_lines)
    request.node.stash[_AUDIT_SECTION_KEY] = section
    request.config.stash[_AUDIT_RESULTS_KEY].append(
        (request.node.nodeid, str(log_dir), unexpected, known)
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

    unexpected_count = sum(len(unexpected) for _, _, unexpected, _ in results)
    unexpected_test_count = sum(bool(unexpected) for _, _, unexpected, _ in results)
    known_count = sum(len(known) for _, _, _, known in results)
    known_test_count = sum(bool(known) for _, _, _, known in results)
    if unexpected_count:
        terminalreporter.write_line(
            f"Found {unexpected_count} unexpected suspicious line(s) in "
            f"{unexpected_test_count} test(s); tests were not failed."
        )
    else:
        terminalreporter.write_line(
            f"No unexpected suspicious serial output found in {log_count} DUT/peer log(s)."
        )
    if known_count:
        terminalreporter.write_line(
            f"Found {known_count} known allowed line(s) in {known_test_count} test(s)."
        )

    for nodeid, log_dir, unexpected, known in results:
        terminalreporter.write_line(nodeid)
        terminalreporter.write_line(f"  Log directory: {log_dir}")
        for finding in unexpected:
            terminalreporter.write_line(f"  {finding}")
        for finding, reason in known:
            terminalreporter.write_line(f"  KNOWN: {finding}")
            terminalreporter.write_line(f"    Reason: {reason}")
