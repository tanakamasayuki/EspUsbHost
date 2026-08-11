"""Host tests for the USBTMC message layer.

`UsbtmcProtocol.hpp` in examples/Vendor/EspUsbHostUsbtmcScpi is deliberately
free of Arduino and USB dependencies, so like the turing and dl1xx tests this one
needs no extraction step: it compiles the production header directly with g++.

Covered:
- the 4-byte alignment rule every USBTMC message ends on
- the bTag sequence, which must never produce 0 or repeat the previous tag
- the 12-byte DEV_DEP_MSG_OUT and REQUEST_DEV_DEP_MSG_IN headers, byte for byte,
  including the little-endian TransferSize and the EOM / TermChar attributes
- response header decoding against headers built independently from the field
  layout, including every out-of-sync case that must be rejected rather than
  trusted (wrong MsgID, bad bTagInverse, bTag 0, TransferSize past the read)
- the GET_CAPABILITIES bit fields, notably the USB488 SCPI bit
- the class / standard request constants the control transfers are addressed with
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Vendor" / "EspUsbHostUsbtmcScpi"


def test_usbtmc_protocol():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "usbtmc_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "usbtmc_test.cpp"),
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
