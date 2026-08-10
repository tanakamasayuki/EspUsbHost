"""Host tests for the FeliCa and RC-S300 protocol layers.

`FelicaProtocol.hpp` and `Rcs300Protocol.hpp` in
examples/Ccid/EspUsbHostCcidFelicaIdm are deliberately free of Arduino and USB
dependencies, so like the ax206 test this one needs no extraction step: it
compiles the production headers directly with g++.

Covered:
- the FeliCa Polling frame for the transit System Code 0x0003 and for the
  wildcard 0xffff, byte for byte, including the length byte that counts itself
- the Polling answer with and without request data, the answering System Code,
  trailing bytes after the declared length, and the rejects (wrong response
  code, a declared length that is too small, too large, or past the buffer)
- every RC-S300 pseudo APDU byte for byte against what tests/probe/rcs300_felica
  actually sent: the four manage session commands, switch protocol to FeliCa, and
  a transparent exchange carrying the Polling
- the response objects the reader actually returned: accepted, switch protocol
  answering 8F 01 08, an exchange with nothing in the field, and the four
  refusals (6301, 6401, 6700, 6A81)
- a successful exchange in the shape a real reader answered one with (status
  object, 92 01 00, 96 02 00 00, then the frame in a 97 object), decoded all the
  way from response bytes to an IDm, plus
  the rejects (no status object, an object length that overruns, a status object
  too short, a two byte tag)
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Ccid" / "EspUsbHostCcidFelicaIdm"


def test_felica_idm():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "felica_idm_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "felica_idm_test.cpp"),
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
