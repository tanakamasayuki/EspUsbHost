"""Host tests for the AX206 protocol layer.

`Ax206Protocol.hpp` in examples/Vendor/EspUsbHostDisplayAx206 is deliberately
free of Arduino, LovyanGFX and USB dependencies, so like the dl1xx test this one
needs no extraction step: it compiles the production header directly with g++.

Covered:
- the two 16-byte command blocks the MIT reference contains, byte for byte; the
  rest of the layer is derived from them
- the blit rectangle, whose corners are inclusive, and its data length
- the rectangle bounds guard
- the 31-byte Command Block Wrapper: little-endian tag and transfer length,
  direction flag, LUN and command length
- the Command Status Wrapper, located by signature so a whole packet can be
  handed over, including tag mismatch, truncation and leading stray bytes
- RGB565 big-endian pixel bytes, the layout that lets LovyanGFX's rgb565_2Byte
  output reach USB without a byte swap
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Vendor" / "EspUsbHostDisplayAx206"


def test_ax206_protocol():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "ax206_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "ax206_test.cpp"),
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
