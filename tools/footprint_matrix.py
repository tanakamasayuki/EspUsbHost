#!/usr/bin/env python3
"""Measure EspUsbHost Flash and static RAM across library releases.

The Arduino core, target profile, and probe sketches stay fixed while the
EspUsbHost git ref changes. This isolates library-version trends from core and
example changes. Successful builds retain their ELF, map, application bin, and
compiler log under --artifacts-dir; compact normalized metrics go to JSON.

Examples:
  python tools/footprint_matrix.py --library-versions auto --target esp32s3
  python tools/footprint_matrix.py --library-versions v2.3.0,v2.3.1,WORKTREE --list
  python tools/footprint_matrix.py --render-from artifacts --output docs/FOOTPRINT.md
"""

import argparse
import contextlib
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
PROBE_ROOT = REPO_ROOT / "tools" / "footprint_sketches"
DEFAULT_CORE_SOURCE = REPO_ROOT / "examples" / "Info" / "EspUsbHostDeviceInfo" / "sketch.yaml"
PLATFORM_INDEX_URL = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"

TARGETS = {
    "esp32s3": "esp32:esp32:esp32s3:USBMode=default",
    "esp32s2": "esp32:esp32:esp32s2",
    "esp32p4": "esp32:esp32:esp32p4",
}

# category, minimum library version, supported targets
PROBES = [
    ("Base", (2, 0, 0), tuple(TARGETS)),
    ("HID", (2, 0, 0), tuple(TARGETS)),
    ("Serial", (2, 0, 0), tuple(TARGETS)),
    ("Audio", (2, 0, 0), tuple(TARGETS)),
    ("Storage", (2, 0, 0), tuple(TARGETS)),
    ("MIDI", (2, 0, 0), tuple(TARGETS)),
    ("Vendor", (2, 1, 0), tuple(TARGETS)),
    ("Network", (2, 2, 0), tuple(TARGETS)),
    ("Ccid", (2, 7, 1), tuple(TARGETS)),
    ("Info", (2, 0, 0), tuple(TARGETS)),
]

PASS, FAIL, UNAVAILABLE, NO_TARGET = "pass", "fail", "unavailable", "no-target"

PROGRAM_RE = re.compile(
    r"Sketch uses\s+(\d+)\s+bytes\s+\((\d+)%\).*?Maximum is\s+(\d+)\s+bytes",
    re.DOTALL,
)
RAM_RE = re.compile(
    r"Global variables use\s+(\d+)\s+bytes\s+\((\d+)%\).*?leaving\s+(\d+)\s+bytes.*?Maximum is\s+(\d+)\s+bytes",
    re.DOTALL,
)


def version_tuple(value: str) -> tuple[int, ...]:
    match = re.search(r"(?:^|[^0-9])(\d+)\.(\d+)\.(\d+)", value)
    return tuple(int(part) for part in match.groups()) if match else ()


def git(*args: str, cwd: pathlib.Path = REPO_ROOT, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["git", "-C", str(cwd), *args],
        check=check,
        capture_output=True,
        text=True,
    )


def discover_library_refs() -> list[str]:
    proc = git("tag", "--list", "v2.*")
    refs = [line.strip() for line in proc.stdout.splitlines() if version_tuple(line)]
    return sorted(refs, key=version_tuple)


def resolve_library_refs(value: str) -> list[str]:
    if value == "auto":
        refs = discover_library_refs()
    else:
        refs = [part.strip() for part in value.split(",") if part.strip()]
    if not refs:
        raise SystemExit("No library versions selected.")
    return refs


def resolve_core_version(value: str) -> str:
    if value != "auto":
        return value
    text = DEFAULT_CORE_SOURCE.read_text()
    match = re.search(r"platform:\s*esp32:esp32\s*\(([^)]+)\)", text)
    if not match:
        raise SystemExit(f"Could not find the pinned esp32 core version in {DEFAULT_CORE_SOURCE}")
    return match.group(1).strip()


def read_library_version(root: pathlib.Path) -> str:
    match = re.search(r"^version=(.+)$", (root / "library.properties").read_text(), re.MULTILINE)
    return match.group(1).strip() if match else "unknown"


@contextlib.contextmanager
def library_tree(ref: str, temporary_root: pathlib.Path):
    if ref == "WORKTREE":
        yield REPO_ROOT
        return

    path = temporary_root / ("library-" + re.sub(r"[^A-Za-z0-9_.-]", "_", ref))
    proc = git("worktree", "add", "--detach", str(path), ref, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"git worktree add {ref} failed: {proc.stderr.strip()}")
    try:
        yield path
    finally:
        git("worktree", "remove", "--force", str(path), check=False)


def revision_for(root: pathlib.Path) -> str:
    return git("rev-parse", "HEAD", cwd=root).stdout.strip()


def probe_revision() -> str:
    digest = hashlib.sha256()
    for path in sorted(PROBE_ROOT.rglob("*.ino")):
        digest.update(str(path.relative_to(PROBE_ROOT)).encode())
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return "sha256:" + digest.hexdigest()


def profile_yaml(target: str, core_version: str, library_root: pathlib.Path) -> str:
    return f"""profiles:
  footprint:
    fqbn: {TARGETS[target]}
    platforms:
      - platform: esp32:esp32 ({core_version})
        platform_index_url: {PLATFORM_INDEX_URL}
    libraries:
      - dir: {json.dumps(str(library_root))}

default_profile: footprint
"""


def error_summary(output: str) -> str:
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    for line in lines:
        if "error:" in line:
            return line
    return lines[-1] if lines else "build failed"


def parse_compile_metrics(output: str) -> dict:
    metrics = {}
    program = PROGRAM_RE.search(output)
    if program:
        metrics.update(
            program_bytes=int(program.group(1)),
            program_percent=int(program.group(2)),
            program_max_bytes=int(program.group(3)),
        )
    ram = RAM_RE.search(output)
    if ram:
        metrics.update(
            static_ram_bytes=int(ram.group(1)),
            static_ram_percent=int(ram.group(2)),
            static_ram_remaining_bytes=int(ram.group(3)),
            static_ram_max_bytes=int(ram.group(4)),
        )
    return metrics


def find_size_tool(target: str) -> pathlib.Path | None:
    executable = {
        "esp32s3": "xtensa-esp32s3-elf-size",
        "esp32s2": "xtensa-esp32s2-elf-size",
        "esp32p4": "riscv32-esp-elf-size",
    }[target]
    found = shutil.which(executable)
    if found:
        return pathlib.Path(found)
    data_dir = pathlib.Path(os.environ.get("ARDUINO_DIRECTORIES_DATA", pathlib.Path.home() / ".arduino15"))
    candidates = sorted(data_dir.glob(f"packages/esp32/tools/**/bin/{executable}"))
    return candidates[-1] if candidates else None


def elf_sections(elf: pathlib.Path, target: str) -> dict[str, int]:
    tool = find_size_tool(target)
    if not tool:
        return {}
    proc = subprocess.run([str(tool), "-A", str(elf)], capture_output=True, text=True)
    if proc.returncode != 0:
        return {}
    sections = {}
    for line in proc.stdout.splitlines():
        match = re.match(r"^\s*(\.[^\s]+)\s+(\d+)\s+", line)
        if match:
            sections[match.group(1)] = int(match.group(2))
    return sections


def copy_build_artifacts(build_dir: pathlib.Path, destination: pathlib.Path, log: str, target: str) -> dict:
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "compile.log").write_text(log)
    saved = {"log": str(destination / "compile.log")}

    patterns = {"elf": "*.ino.elf", "map": "*.ino.map", "bin": "*.ino.bin"}
    for kind, pattern in patterns.items():
        matches = list(build_dir.glob(pattern))
        if not matches:
            continue
        source = matches[0]
        target_path = destination / source.name
        shutil.copy2(source, target_path)
        saved[kind] = str(target_path)
        saved[f"{kind}_bytes"] = target_path.stat().st_size
        if kind == "elf":
            saved["sections"] = elf_sections(target_path, target)
    return saved


def relative_artifacts(saved: dict, artifact_root: pathlib.Path) -> dict:
    result = dict(saved)
    for key in ("log", "elf", "map", "bin"):
        if key in result:
            result[key] = str(pathlib.Path(result[key]).relative_to(artifact_root))
    return result


def selected_probes(value: str) -> list[tuple[str, tuple[int, ...], tuple[str, ...]]]:
    if not value:
        return PROBES
    wanted = {part.strip() for part in value.split(",") if part.strip()}
    known = {probe[0] for probe in PROBES}
    unknown = wanted - known
    if unknown:
        raise SystemExit(f"Unknown probe(s): {', '.join(sorted(unknown))}")
    return [probe for probe in PROBES if probe[0] in wanted]


def build_one(
    library_root: pathlib.Path,
    library_ref: str,
    library_version: str,
    target: str,
    core_version: str,
    probe: str,
    workspace: pathlib.Path,
    artifact_root: pathlib.Path,
) -> dict:
    probe_dir = workspace / "probe" / probe
    build_dir = workspace / "build" / probe
    shutil.copytree(PROBE_ROOT / probe, probe_dir)
    (probe_dir / "sketch.yaml").write_text(profile_yaml(target, core_version, library_root))
    build_dir.mkdir(parents=True)

    started = time.monotonic()
    proc = subprocess.run(
        [
            "arduino-cli", "compile", "--profile", "footprint", "--clean",
            "--build-path", str(build_dir), str(probe_dir),
        ],
        capture_output=True,
        text=True,
    )
    elapsed = time.monotonic() - started
    output = (proc.stdout or "") + (proc.stderr or "")
    destination = artifact_root / target / re.sub(r"[^A-Za-z0-9_.-]", "_", library_ref) / probe
    saved = copy_build_artifacts(build_dir, destination, output, target)

    row = {
        "category": probe,
        "library_ref": library_ref,
        "library_version": library_version,
        "target": target,
        "core_version": core_version,
        "state": PASS if proc.returncode == 0 else FAIL,
        "note": "" if proc.returncode == 0 else error_summary(output),
        "build_seconds": round(elapsed, 3),
        "artifacts": relative_artifacts(saved, artifact_root),
    }
    if proc.returncode == 0:
        row.update(parse_compile_metrics(output))
        if "program_bytes" not in row or "static_ram_bytes" not in row:
            row["state"] = FAIL
            row["note"] = "build passed but Arduino size summary could not be parsed"
    return row


def skipped_row(
    probe: str,
    ref: str,
    version: str,
    target: str,
    core: str,
    state: str,
    note: str,
    commit: str = "",
) -> dict:
    row = {
        "category": probe,
        "library_ref": ref,
        "library_version": version,
        "target": target,
        "core_version": core,
        "state": state,
        "note": note,
    }
    if commit:
        row["library_commit"] = commit
    return row


def build_mode(args) -> int:
    if args.target not in TARGETS:
        raise SystemExit(f"Unknown target: {args.target}")
    refs = resolve_library_refs(args.library_versions)
    core_version = resolve_core_version(args.core_version)
    probes = selected_probes(args.probes)

    if args.list:
        print(f"Core: {core_version}")
        print(f"Target: {args.target}")
        print(f"Library refs: {', '.join(refs)}")
        for name, minimum, targets in probes:
            print(f"  {name}: min={'.'.join(map(str, minimum))} targets={','.join(targets)}")
        return 0

    if not shutil.which("arduino-cli"):
        print("arduino-cli not found on PATH", file=sys.stderr)
        return 2
    if not args.json_out or not args.artifacts_dir:
        print("build mode requires --json and --artifacts-dir", file=sys.stderr)
        return 2

    artifact_root = pathlib.Path(args.artifacts_dir).resolve()
    artifact_root.mkdir(parents=True, exist_ok=True)
    results = []
    probe_hash = probe_revision()

    with tempfile.TemporaryDirectory(prefix="espusb-footprint-") as temporary:
        temporary_root = pathlib.Path(temporary)
        for ref in refs:
            try:
                with library_tree(ref, temporary_root) as library_root:
                    version = read_library_version(library_root)
                    commit = revision_for(library_root)
                    parsed_version = version_tuple(version)
                    print(f"==> {ref} (version={version}, commit={commit[:12]})", flush=True)
                    for probe, minimum, targets in probes:
                        if args.target not in targets:
                            results.append(skipped_row(
                                probe, ref, version, args.target, core_version,
                                NO_TARGET, f"{probe} has no {args.target} footprint profile", commit,
                            ))
                            continue
                        if parsed_version and parsed_version < minimum:
                            results.append(skipped_row(
                                probe, ref, version, args.target, core_version,
                                UNAVAILABLE, f"{probe} requires EspUsbHost >= {'.'.join(map(str, minimum))}", commit,
                            ))
                            continue
                        workspace = temporary_root / "build-work" / re.sub(r"[^A-Za-z0-9_.-]", "_", ref)
                        if workspace.exists():
                            shutil.rmtree(workspace)
                        row = build_one(
                            library_root, ref, version, args.target, core_version,
                            probe, workspace, artifact_root,
                        )
                        row["library_commit"] = commit
                        results.append(row)
                        value = row.get("program_bytes", "-")
                        print(f"    {probe:<8} {row['state']:<5} flash={value}", flush=True)
            except RuntimeError as error:
                print(f"ERROR: {error}", file=sys.stderr)
                for probe, _, _ in probes:
                    results.append(skipped_row(
                        probe, ref, "unknown", args.target, core_version, FAIL, str(error),
                    ))

    payload = {
        "schema_version": 1,
        "core_version": core_version,
        "probe_revision": probe_hash,
        "target": args.target,
        "results": results,
    }
    output = pathlib.Path(args.json_out)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
    print(f"Wrote {output}")
    return 0


def merge_payloads(payloads: list[dict]) -> dict:
    core_versions = {payload.get("core_version") for payload in payloads}
    probe_revisions = {payload.get("probe_revision") for payload in payloads}
    if len(core_versions) != 1:
        raise SystemExit(f"Cannot merge differing core versions: {sorted(core_versions)}")
    if len(probe_revisions) != 1:
        raise SystemExit("Cannot merge results from differing footprint probe revisions")
    results = []
    for payload in payloads:
        for row in payload.get("results", []):
            # Build time is useful in the temporary per-target artifact, but it is
            # cache/runner dependent and would make the committed dataset change
            # on every identical rerun.
            results.append({key: value for key, value in row.items() if key != "build_seconds"})
    return {
        "schema_version": 1,
        "core_version": next(iter(core_versions)),
        "probe_revision": next(iter(probe_revisions)),
        "targets": [target for target in TARGETS if any(row.get("target") == target for row in results)],
        "results": results,
    }


def format_size(value: int | None) -> str:
    return "—" if value is None else f"{value / 1024:.1f} KiB"


def metric_cell(row: dict | None, baseline: dict | None, key: str) -> str:
    if not row:
        return "—"
    if row.get("state") == FAIL:
        return "❌"
    if row.get("state") in (UNAVAILABLE, NO_TARGET):
        return "—"
    value = row.get(key)
    if value is None:
        return "?"
    if row.get("category") == "Base" or not baseline or baseline.get(key) is None:
        return format_size(value)
    delta = value - baseline[key]
    sign = "+" if delta >= 0 else ""
    return f"{format_size(value)} ({sign}{delta / 1024:.1f})"


def render_markdown(payload: dict, json_path: str) -> str:
    results = payload["results"]
    categories = [probe[0] for probe in PROBES if any(row["category"] == probe[0] for row in results)]
    refs = []
    for row in results:
        key = (row["library_ref"], row["library_version"])
        if key not in refs:
            refs.append(key)
    refs.sort(key=lambda item: (version_tuple(item[1]), item[0] == "WORKTREE", item[0]))
    lookup = {
        (row["target"], row["library_ref"], row["category"]): row
        for row in results
    }

    lines = [
        f"# EspUsbHost footprint — arduino-esp32 {payload['core_version']}",
        "",
        f"- Arduino core: `esp32:esp32@{payload['core_version']}`",
        f"- Probe revision: `{payload['probe_revision']}`",
        f"- Normalized source data: `{json_path}`",
        "- Values in parentheses are the feature delta from the Base probe, in KiB.",
        "",
        "Legend: ❌ build/measurement failure · — feature unavailable or target not supported",
        "",
    ]

    for target in payload["targets"]:
        lines.extend([f"## {target}", ""])
        for title, key in (("Flash", "program_bytes"), ("Static RAM", "static_ram_bytes")):
            lines.extend([f"### {title}", ""])
            lines.append("| Library | " + " | ".join(categories) + " |")
            lines.append("| --- | " + " | ".join("---:" for _ in categories) + " |")
            for ref, version in refs:
                baseline = lookup.get((target, ref, "Base"))
                cells = [metric_cell(lookup.get((target, ref, category)), baseline, key) for category in categories]
                label = f"`{ref}` ({version})" if ref != version and ref != f"v{version}" else f"`{ref}`"
                lines.append(f"| {label} | " + " | ".join(cells) + " |")
            lines.append("")

    failures = [row for row in results if row.get("state") == FAIL]
    if failures:
        lines.extend(["## Failures", ""])
        for row in failures:
            lines.append(
                f"- `{row['library_ref']}` / `{row['target']}` / `{row['category']}`: {row.get('note', 'failed')}"
            )
        lines.append("")
    return "\n".join(lines)


def render_mode(args) -> int:
    input_dir = pathlib.Path(args.render_from)
    payloads = [json.loads(path.read_text()) for path in sorted(input_dir.rglob("*.json"))]
    if not payloads:
        print(f"No JSON payloads found under {input_dir}", file=sys.stderr)
        return 2
    merged = merge_payloads(payloads)

    if not args.output or not args.merged_json:
        print("render mode requires --output and --merged-json", file=sys.stderr)
        return 2
    merged_path = pathlib.Path(args.merged_json)
    merged_path.parent.mkdir(parents=True, exist_ok=True)
    merged_path.write_text(json.dumps(merged, indent=2, ensure_ascii=False) + "\n")

    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        json_label = str(merged_path.relative_to(REPO_ROOT))
    except ValueError:
        json_label = str(merged_path)
    output.write_text(render_markdown(merged, json_label))
    print(f"Merged {len(payloads)} payload(s) -> {output} and {merged_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library-versions", default="auto", help="comma-separated git refs, or auto for all v2.* tags")
    parser.add_argument(
        "--core-version",
        default="auto",
        help="fixed arduino-esp32 core version; auto reads the repository's representative sketch.yaml",
    )
    parser.add_argument("--target", default="esp32s3", choices=TARGETS, help="target profile to build")
    parser.add_argument("--probes", default="", help="comma-separated probe categories (default: all)")
    parser.add_argument("--json", dest="json_out", default="", help="per-target JSON output")
    parser.add_argument("--artifacts-dir", default="", help="directory for logs, ELF, map, and application bin")
    parser.add_argument("--render-from", default="", help="merge per-target JSON files found under this directory")
    parser.add_argument("--output", default="", help="rendered Markdown output")
    parser.add_argument("--merged-json", default="", help="normalized merged JSON output")
    parser.add_argument("--list", action="store_true", help="show the resolved build plan without compiling")
    args = parser.parse_args()
    return render_mode(args) if args.render_from else build_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
