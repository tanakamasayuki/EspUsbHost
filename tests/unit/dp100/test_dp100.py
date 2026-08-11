"""Host tests for the ALIENTEK DP100 frame layer.

`Dp100Protocol.hpp` in examples/HID/EspUsbHostDp100Power is deliberately free of
Arduino and USB dependencies, so like the usbtmc and dl1xx tests this one needs no
extraction step: it compiles the production header directly with g++.

Covered:
- CRC-16/MODBUS against the canonical "123456789" check value and against a
  second, table-driven implementation over every length up to a full report
- the request frame byte for byte, including where the CRC lands once there is
  data and that the rest of the report stays zero
- response decoding against reports captured from a real DP100 by
  tests/probe/dp100, plus every case that must be rejected rather than trusted
  (short read, wrong direction byte, bad CRC, corrupted body, length past the end)
- the one-byte 0x00 refusal the device answers a bad frame with
- the DEVICE_INFO and BASIC_INFO field offsets and units, pinned to those captures
- the BASIC_SET round trip, whose offsets are not hardware-confirmed
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "HID" / "EspUsbHostDp100Power"


def test_dp100_protocol():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "dp100_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "dp100_test.cpp"),
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
