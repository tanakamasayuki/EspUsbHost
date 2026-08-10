"""Host tests for the 3.5-inch USB smart screen protocol layer.

`TuringProtocol.hpp` in examples/Serial/EspUsbHostDisplayTuring is
deliberately free of Arduino, LovyanGFX and USB dependencies, so like the dl1xx
test this one needs no extraction step: it compiles the production header
directly with g++.

Covered:
- the 6-byte command packet: four 10-bit coordinates round-tripped against an
  independent decoder, one field at a time and exhaustively over the panel's
  coordinate space
- the DISPLAY_BITMAP rectangle, which is inclusive on both ends
- the rectangle bounds guard, for both the panel size and the 10-bit packing
- the 11-byte orientation packet, including its big-endian size fields
- brightness, whose levels run backwards on the wire
- RGB565 little-endian pixel bytes, the layout that lets LovyanGFX's
  rgb565_nonswapped output go to USB with no byte swapping
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Serial" / "EspUsbHostDisplayTuring"


def test_turing_protocol():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "turing_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "turing_test.cpp"),
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
