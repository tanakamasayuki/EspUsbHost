"""Host tests for the DL-1xx protocol layer.

`Dl1xxProtocol.hpp` and `Dl1xxModes.hpp` in
examples/Vendor/EspUsbHostDisplayDl1xx are deliberately free of Arduino,
LovyanGFX and USB dependencies, so unlike the keymap test this one needs no
extraction step: it compiles the production headers directly with g++.

Covered:
- the 16-bit LFSR used to encode timing register values, including the
  maximal-length property that pins the tap set
- register write byte order (16-bit high-first, the pixel clock's low-first
  exception, 24-bit plane addresses), lock / unlock / flush / padding
- the RLE pixel encoder: the documented 10-byte solid-run form, the 519-byte
  worst case, round trips against an independent decoder, and buffer limits with
  a canary that catches an overrun
- the mode table (implied refresh rate, 24-bit addressability, pixel clock
  representable in 5 kHz units) and the full Full HD mode-set register stream
"""

from pathlib import Path

HERE = Path(__file__).parent
REPO = HERE.parents[2]
EXAMPLE = REPO / "examples" / "Vendor" / "EspUsbHostDisplayDl1xx"


def test_dl1xx_protocol(build_and_run):
    print(build_and_run("dl1xx_test.cpp", includes=[EXAMPLE]))
