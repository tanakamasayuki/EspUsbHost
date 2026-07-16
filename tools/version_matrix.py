#!/usr/bin/env python3
"""Build one EspUsbHost library version against many arduino-esp32 core versions.

Given a single library version (a git tag/ref, or the current working tree), this
builds a representative example per feature category for every requested core
version x target profile, and writes a compatibility matrix as Markdown (and JSON).

The library version axis is deliberately ONE per run: to cover several library
versions, invoke this once per version (the CI workflow does exactly that). The
script only *generates* the report files; committing them is the workflow's job.

Mechanism: the chosen library ref is materialised in a throwaway git worktree, and
for each core version the example's `sketch.yaml` platform pin is rewritten to that
version before `arduino-cli compile --profile <target>` runs. arduino-cli
auto-installs the pinned core version on first use (same as tools/build_check.py),
so no separate `core install` step is needed. Building inside the worktree means
each example links against *that library version's* src/ tree via its
`libraries: - dir: ../../../` declaration.

Examples:
  # current working tree, a couple of cores, default representative examples
  python tools/version_matrix.py --core-versions 3.2.1,3.3.10

  # a released tag, auto-discovered core versions from the package index (>= 3.2.0)
  python tools/version_matrix.py --lib-version v2.2.0 --core-versions auto

  # dry run: show what would build, no compiles
  python tools/version_matrix.py --core-versions 3.3.10 --list
"""

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
PACKAGE_INDEX_URL = "https://espressif.github.io/arduino-esp32/package_esp32_index.json"
# Floor for `auto` core discovery. Supported baseline is arduino-esp32 3.2.0
# (S2/S3); 3.1.x and older are not supported. Pass --core-versions explicitly to
# probe below this floor.
CORE_VERSION_FLOOR = (3, 2, 0)

# One representative example per feature category. Paths are relative to examples/.
# Chosen to avoid external-library dependencies so a clean build reflects the core
# x library compatibility, not a missing third-party lib. Override with --examples.
DEFAULT_EXAMPLES = [
    ("HID", "HID/EspUsbHostKeyboard"),
    ("Serial", "Serial/EspUsbHostUSBSerial"),
    ("Audio", "Audio/EspUsbHostAudioOutputTone"),
    ("Storage", "Storage/EspUsbHostMSCFatList"),
    ("MIDI", "MIDI/EspUsbHostMIDI"),
    ("Vendor", "Vendor/EspUsbHostVendorBulk"),
    ("Network", "UsbNetwork"),
    ("Info", "Info/EspUsbHostDeviceInfo"),
]

DEFAULT_TARGETS = ["esp32s3", "esp32s2", "esp32p4"]

# Result cell states.
PASS, FAIL, ABSENT, NO_PROFILE, NA_BOARD = "pass", "fail", "absent", "no-profile", "na-board"
CELL_GLYPH = {PASS: "✅", FAIL: "❌", ABSENT: "—", NO_PROFILE: "·", NA_BOARD: "·"}
CELL_NOTE = {
    ABSENT: "example not present in this library version",
    NO_PROFILE: "no such profile in the example's sketch.yaml",
    NA_BOARD: "target board not available in this core version",
}

# Substrings in arduino-cli output that mean "this board isn't in this core version"
# rather than a genuine library/build failure.
NA_BOARD_MARKERS = (
    "not found in platform",
    "Invalid FQBN",
    "board not found",
    "Unknown FQBN",
    "unknown package",
)


def profiles_in(sketch_yaml: pathlib.Path) -> list[str]:
    """Return the profile names declared under `profiles:` in a sketch.yaml."""
    names = []
    in_profiles = False
    for line in sketch_yaml.read_text().splitlines():
        if re.match(r"^profiles:\s*$", line):
            in_profiles = True
            continue
        if in_profiles:
            if line and not line[0].isspace():
                break
            m = re.match(r"^  ([A-Za-z0-9_.\-]+):\s*$", line)
            if m:
                names.append(m.group(1))
    return names


def set_platform_version(sketch_yaml: pathlib.Path, version: str) -> None:
    """Rewrite every `platform: esp32:esp32 (x.y.z)` pin to the given version."""
    text = sketch_yaml.read_text()
    new = re.sub(
        r"(platform:\s*esp32:esp32\s*\()[^)]+(\))",
        lambda m: f"{m.group(1)}{version}{m.group(2)}",
        text,
    )
    if new != text:
        sketch_yaml.write_text(new)


def use_local_library(sketch_yaml: pathlib.Path, lib_root: pathlib.Path) -> None:
    """Point the EspUsbHost dependency at the checked-out source, not the registry.

    Released tags reference the *published* library (`- EspUsbHost (x.y.z)`), which
    arduino-cli would download from the Arduino library index — so a plain build would
    test the registry copy (or fail outright for a version not yet published) instead
    of the code at this ref. Rewrite that entry to `- dir: <lib_root>` so the example
    compiles against THIS ref's src/. Other libraries (ESP8266Audio, etc.) are left
    untouched. Uses a relative path so it matches the dev-tree `dir: ../../../` form.
    """
    text = sketch_yaml.read_text()
    rel = os.path.relpath(lib_root, sketch_yaml.parent)
    new = re.sub(
        r"^(\s*)-\s*EspUsbHost\s*\([^)]*\)\s*$",
        lambda m: f"{m.group(1)}- dir: {rel}",
        text,
        flags=re.MULTILINE,
    )
    if new != text:
        sketch_yaml.write_text(new)


def discover_core_versions() -> list[str]:
    """Fetch released esp32:esp32 core versions (>= floor) from the package index."""
    with urllib.request.urlopen(PACKAGE_INDEX_URL, timeout=30) as resp:
        data = json.load(resp)
    versions = set()
    for pkg in data.get("packages", []):
        if pkg.get("name") != "esp32":
            continue
        for plat in pkg.get("platforms", []):
            if plat.get("architecture") == "esp32":
                versions.add(plat.get("version"))
    return sorted((v for v in versions if _ver_tuple(v) >= CORE_VERSION_FLOOR), key=_ver_tuple)


def _ver_tuple(v: str) -> tuple:
    parts = re.findall(r"\d+", v)
    return tuple(int(p) for p in parts[:3]) + (0,) * (3 - len(parts[:3]))


def resolve_lib_worktree(lib_ref: str, stack) -> tuple[pathlib.Path, str]:
    """Return (root_path, lib_version_string) for the requested library ref.

    lib_ref == 'WORKTREE' uses the current tree in place; otherwise a detached git
    worktree is created for the ref and cleaned up when `stack` unwinds.
    """
    if lib_ref == "WORKTREE":
        return REPO_ROOT, _read_lib_version(REPO_ROOT) + "+wt"
    # Fail early with a clear message if the ref does not exist, rather than letting
    # `git worktree add` dump a CalledProcessError traceback. A not-yet-created release
    # tag is the common cause; suggest WORKTREE or an existing ref.
    verify = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "--verify", "--quiet", f"{lib_ref}^{{commit}}"],
        capture_output=True, text=True,
    )
    if verify.returncode != 0:
        raise SystemExit(
            f"error: library ref '{lib_ref}' not found in this repository.\n"
            f"  - Check the spelling (e.g. 'v2.3.1', not 'v.2.3.1').\n"
            f"  - The tag must already exist and be fetched (CI checkout needs fetch-depth: 0).\n"
            f"  - To test an unreleased version, pass WORKTREE (current checkout) or a branch/commit."
        )
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="espusbhost-wt-"))
    proc = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "worktree", "add", "--detach", str(tmp), lib_ref],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise SystemExit(f"error: `git worktree add {lib_ref}` failed:\n{proc.stderr.strip()}")
    stack.append(tmp)
    return tmp, _read_lib_version(tmp)


def _read_lib_version(root: pathlib.Path) -> str:
    props = (root / "library.properties").read_text()
    m = re.search(r"^version=(.+)$", props, re.MULTILINE)
    return m.group(1).strip() if m else "unknown"


def classify(returncode: int, output: str) -> str:
    if returncode == 0:
        return PASS
    if any(marker in output for marker in NA_BOARD_MARKERS):
        return NA_BOARD
    return FAIL


def error_summary(output: str) -> str:
    """Pick the most informative line from a failed build's output.

    The real compiler diagnostic (`... error: ...`) sits in the middle of the log;
    arduino-cli's own last line is just "Error during build: exit status 1". Prefer
    the first `error:` line, falling back to the last non-empty line.
    """
    lines = [ln.strip() for ln in output.splitlines() if ln.strip()]
    for ln in lines:
        if "error:" in ln:
            return ln
    return lines[-1] if lines else ""


def render_markdown(lib_version: str, lib_ref: str, core_versions, targets, examples, results) -> str:
    """results[(cat, path, target, core)] -> (state, note)."""
    lines = []
    lines.append(f"# EspUsbHost {lib_version} — arduino-esp32 core compatibility")
    lines.append("")
    lines.append(f"- Library ref: `{lib_ref}`")
    lines.append(f"- Targets: {', '.join(targets)}")
    lines.append(f"- Core versions: {', '.join(core_versions)}")
    lines.append("")
    lines.append("Legend: ✅ builds · ❌ fails · — example absent in this version · · not applicable (no profile / board not in core)")
    lines.append("")

    for target in targets:
        lines.append(f"## {target}")
        lines.append("")
        header = "| Feature (example) | " + " | ".join(core_versions) + " |"
        sep = "| --- | " + " | ".join("---" for _ in core_versions) + " |"
        lines.append(header)
        lines.append(sep)
        for cat, path in examples:
            cells = []
            for core in core_versions:
                state, _ = results[(cat, path, target, core)]
                cells.append(CELL_GLYPH[state])
            lines.append(f"| {cat} (`{path}`) | " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines) + "\n"


def build_payload(lib_version, lib_ref, core_versions, targets, results) -> dict:
    return {
        "lib_version": lib_version,
        "lib_ref": lib_ref,
        "core_versions": core_versions,
        "targets": targets,
        "results": [
            {"category": cat, "example": path, "target": t, "core": c, "state": st, "note": nt}
            for (cat, path, t, c), (st, nt) in results.items()
        ],
    }


def merge_payloads(payloads: list[dict]):
    """Combine per-core JSON payloads (same library version) for rendering.

    Returns (lib_version, lib_ref, core_versions, targets, examples, results) ready
    to hand to render_markdown. Later core columns simply add to the matrix.
    """
    lib_versions = {p.get("lib_version") for p in payloads}
    if len(lib_versions) > 1:
        print(f"WARNING: merging payloads with differing lib_version: {lib_versions}", file=sys.stderr)
    lib_version = payloads[0].get("lib_version", "unknown")
    lib_ref = payloads[0].get("lib_ref", "")

    targets = []
    examples = []
    seen_examples = set()
    core_versions = set()
    results = {}
    for p in payloads:
        for t in p.get("targets", []):
            if t not in targets:
                targets.append(t)
        for row in p.get("results", []):
            cat, path, t, c = row["category"], row["example"], row["target"], row["core"]
            core_versions.add(c)
            if (cat, path) not in seen_examples:
                seen_examples.add((cat, path))
                examples.append((cat, path))
            results[(cat, path, t, c)] = (row["state"], row.get("note", ""))

    core_versions = sorted(core_versions, key=_ver_tuple)
    # Fill any cell missing from a given core file so render never KeyErrors.
    for cat, path in examples:
        for t in targets:
            for c in core_versions:
                results.setdefault((cat, path, t, c), (ABSENT, ""))
    return lib_version, lib_ref, core_versions, targets, examples, results


def render_from_dir(input_dir: pathlib.Path, output_opt: str) -> int:
    payloads = [json.loads(p.read_text()) for p in sorted(input_dir.glob("*.json"))]
    if not payloads:
        print(f"No *.json payloads found in {input_dir}", file=sys.stderr)
        return 2
    lib_version, lib_ref, core_versions, targets, examples, results = merge_payloads(payloads)
    md = render_markdown(lib_version, lib_ref, core_versions, targets, examples, results)
    output = pathlib.Path(output_opt) if output_opt else REPO_ROOT / "docs" / f"COMPATIBILITY.{lib_version}.md"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(md)
    print(f"Merged {len(payloads)} payload(s) -> {output}")
    print(f"  lib={lib_version} cores={', '.join(core_versions)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--lib-version", default="WORKTREE",
                        help="git tag/ref of the library to test, or WORKTREE (default) for the current tree")
    parser.add_argument("--core-versions", default="auto",
                        help="comma-separated core versions, or 'auto' for released cores >= 3.2.0")
    parser.add_argument("--targets", default=",".join(DEFAULT_TARGETS),
                        help=f"comma-separated profile names (default: {','.join(DEFAULT_TARGETS)})")
    parser.add_argument("--examples", default="",
                        help="comma-separated example paths (relative to examples/); default: representative set")
    parser.add_argument("--output", default="", help="Markdown output path (default: docs/COMPATIBILITY.<libver>.md)")
    parser.add_argument("--json", dest="json_out", default="", help="JSON output path (per-core result payload)")
    parser.add_argument("--json-only", action="store_true",
                        help="build mode: write only the JSON payload, skip the Markdown (for matrix jobs)")
    parser.add_argument("--render-from", default="",
                        help="skip building: merge every *.json in this directory into one Markdown matrix")
    parser.add_argument("--print-cores", action="store_true",
                        help="print the resolved core version list as a JSON array and exit (for CI matrix setup)")
    parser.add_argument("--list", action="store_true", help="print the build plan and exit (no compiles)")
    args = parser.parse_args()

    if args.render_from:
        return render_from_dir(pathlib.Path(args.render_from), args.output)

    targets = [t.strip() for t in args.targets.split(",") if t.strip()]
    if args.examples:
        examples = [("", p.strip()) for p in args.examples.split(",") if p.strip()]
    else:
        examples = DEFAULT_EXAMPLES

    if args.core_versions == "auto":
        core_versions = discover_core_versions()
    else:
        core_versions = [v.strip() for v in args.core_versions.split(",") if v.strip()]
    if not core_versions:
        print("No core versions to build.", file=sys.stderr)
        return 2

    if args.print_cores:
        print(json.dumps(core_versions))
        return 0

    cleanup: list[pathlib.Path] = []
    try:
        root, lib_version = resolve_lib_worktree(args.lib_version, cleanup)
        examples_root = root / "examples"

        print(f"Library: {lib_version} (ref={args.lib_version}, root={root})")
        print(f"Cores  : {', '.join(core_versions)}")
        print(f"Targets: {', '.join(targets)}")
        print(f"Examples: {', '.join(p for _, p in examples)}")
        print()

        if args.list:
            for cat, path in examples:
                sk = examples_root / path / "sketch.yaml"
                exists = sk.exists()
                profs = profiles_in(sk) if exists else []
                print(f"  {cat or '-'}: {path}  {'profiles=' + ','.join(profs) if exists else 'ABSENT'}")
            return 0

        if not shutil.which("arduino-cli"):
            print("arduino-cli not found on PATH", file=sys.stderr)
            return 2

        results = {}
        timings = {}
        n_builds = 0
        run_start = time.monotonic()
        for core in core_versions:
            core_start = time.monotonic()
            for cat, path in examples:
                sketch_dir = examples_root / path
                sk = sketch_dir / "sketch.yaml"
                if not sk.exists():
                    for target in targets:
                        results[(cat, path, target, core)] = (ABSENT, CELL_NOTE[ABSENT])
                    continue
                set_platform_version(sk, core)
                use_local_library(sk, root)
                profs = profiles_in(sk)
                for target in targets:
                    key = (cat, path, target, core)
                    if target not in profs:
                        results[key] = (NO_PROFILE, CELL_NOTE[NO_PROFILE])
                        continue
                    print(f"==> core {core} | {target} | {path}", flush=True)
                    proc = subprocess.run(
                        ["arduino-cli", "compile", "--profile", target, str(sketch_dir)],
                        capture_output=True, text=True,
                    )
                    combined = (proc.stderr or "") + (proc.stdout or "")
                    state = classify(proc.returncode, combined)
                    note = ""
                    if state == FAIL:
                        note = error_summary(combined)
                    elif state in CELL_NOTE:
                        note = CELL_NOTE[state]
                    results[key] = (state, note)
                    n_builds += 1
                    print(f"    {CELL_GLYPH[state]} {state}")
            timings[core] = time.monotonic() - core_start
            print(f"-- core {core} done in {timings[core]:.0f}s", flush=True)

        total_elapsed = time.monotonic() - run_start
        print(f"\nBuilt {n_builds} target(s) across {len(core_versions)} core version(s) "
              f"in {total_elapsed:.0f}s ({total_elapsed/60:.1f} min)")
        for core in core_versions:
            print(f"  core {core}: {timings.get(core, 0):.0f}s")

        payload = build_payload(lib_version, args.lib_version, core_versions, targets, results)

        if not args.json_only:
            md = render_markdown(lib_version, args.lib_version, core_versions, targets, examples, results)
            out_path = pathlib.Path(args.output) if args.output else REPO_ROOT / "docs" / f"COMPATIBILITY.{lib_version}.md"
            out_path.parent.mkdir(parents=True, exist_ok=True)
            out_path.write_text(md)
            print(f"\nWrote {out_path}")

        if args.json_out:
            jp = pathlib.Path(args.json_out)
            jp.parent.mkdir(parents=True, exist_ok=True)
            jp.write_text(json.dumps(payload, indent=2, ensure_ascii=False))
            print(f"Wrote {jp}")
        elif args.json_only:
            print("WARNING: --json-only given without --json; no output written", file=sys.stderr)

        return 0
    finally:
        for wt in cleanup:
            subprocess.run(["git", "-C", str(REPO_ROOT), "worktree", "remove", "--force", str(wt)],
                           capture_output=True, text=True)


if __name__ == "__main__":
    raise SystemExit(main())
