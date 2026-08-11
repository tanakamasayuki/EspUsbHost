"""Host tests for the USB Printer Class requests and the ESC/POS builder.

`PrinterProtocol.hpp`, `EscPos.hpp` and `ReceiptJa.hpp` in
examples/Vendor/EspUsbHostPrinterEscPos are deliberately free of Arduino and USB
dependencies, so like the usbtmc and dp100 tests this one needs no extraction
step: it compiles the production headers directly with g++.

Covered:
- the three class requests' bmRequestType and codes, and the byte-swapped wIndex
  that only GET_DEVICE_ID uses
- GET_PORT_STATUS bit senses, including the two that are inverted (NotError and
  Select are 1 when things are good, PaperEmpty is 1 when the paper is gone)
- the IEEE 1284 device ID: the self-counting big-endian length, a short read
  being rejected rather than trusted, and field lookup that matches whole keys so
  CMDL does not answer a search for CMD
- every ESC/POS command the example emits, byte for byte: the GS ! size packing,
  the cut variants that do and do not take a feed argument, the length-prefixed
  barcode, the five-command QR sequence with its little-endian store length, and
  the GS v 0 raster header
- the builder's overflow behaviour, which is what stops a receipt whose tail (the
  cut, or a command's arguments) was dropped from being sent
- the receipt itself: it fits the shipped buffer, kanji mode is switched off as
  many times as it is switched on, and the ASCII fallback slip stays single-byte
"""

import subprocess
from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Vendor" / "EspUsbHostPrinterEscPos"


def test_escpos_protocol():
    output = HERE / "output"
    output.mkdir(exist_ok=True)

    binary = output / "escpos_test"
    compile_result = subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(EXAMPLE),
            str(HERE / "escpos_test.cpp"),
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
