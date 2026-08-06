"""Host tests for the mouse HID report descriptor parser.

`src/EspUsbHostHidLayout.h` is deliberately free of Arduino and USB
dependencies, so this test compiles the production header directly with g++ --
no extraction step, unlike the keymap test.

Covered:
- the boot mouse descriptor (3 buttons, 8-bit X/Y/wheel), which must produce
  exactly the layout the fixed boot parsing assumed
- the layout reported in issue #39 for a Logitech G502 HERO (16 buttons, 16-bit
  X/Y, wheel and AC Pan in 8 bytes), including the two regressions it caused:
  a Y-only report looking idle, and X movement landing in the wheel
- report IDs: a composite keyboard + mouse descriptor, bit offsets relative to
  the report body, and rejection of a report carrying another ID
- a joystick collection, whose Generic Desktop X / Y must not be taken for a
  mouse
- 12-bit axes packed across byte boundaries, sign extension, and Push / Pop
- rejects: null, empty and truncated descriptors, a collection without Y, and
  decoding with an invalid layout
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
SRC = REPO / "src"


def test_mouse_layout():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "mouse_layout_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(SRC),
            str(HERE / "mouse_layout_test.cpp"),
            "-o",
            str(binary),
        ],
        capture_output=True,
        text=True,
    )
    assert compile_result.returncode == 0, compile_result.stderr

    run_result = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run_result.returncode == 0, run_result.stdout + run_result.stderr
    print(run_result.stdout)
