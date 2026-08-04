"""Host tests for the CCID ATR parser.

`src/EspUsbHostCcidAtr.h` is deliberately free of Arduino and USB dependencies,
so this test compiles the production header directly with g++ -- no extraction
step, unlike the keymap test.

Covered:
- the ATR captured from a real Sony RC-S300 with an ISO 14443 A card
  (`tests/manual/ccid_card`), decoded to standard, level, card name and protocol
- the PC/SC PIX.SS mapping for ISO 14443 A/B, ISO 15693, ISO 7816-10 memory
  cards, FeliCa and low-frequency contactless, plus an unlisted code that must
  keep its raw value rather than be reported as an ISO 7816 card
- a contact card's own ATR: interface-byte walking to find the historical bytes,
  T=1 detection, and the T=0 default when no TD byte names a protocol
- rejected inputs: null, missing T0, invalid TS, truncated historical bytes,
  missing TD1, a wrong RID, and a TLV longer than the historical bytes
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
SRC = REPO / "src"


def test_ccid_atr():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "ccid_atr_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(SRC),
            str(HERE / "ccid_atr_test.cpp"),
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
