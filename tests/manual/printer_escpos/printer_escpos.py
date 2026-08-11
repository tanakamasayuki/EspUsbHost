"""
Purpose:
    Verify the USB Printer Class (interface class 0x07) request layer against a
    real printer: the interface is found and claimed through the vendor bulk API,
    GET_DEVICE_ID / GET_PORT_STATUS / SOFT_RESET run on EP0 via
    vendorControlTransfer(), and ESC/POS real-time status (DLE EOT n) is exchanged
    on the bulk endpoints.

Why manual:
    Requires a physical USB printer. No peer device implements the printer class,
    so it cannot be exercised against the sibling EspUsbDevice library.

Required hardware:
    - ESP32-S3 host board
    - ESC/POS USB receipt printer (developed against an Xprinter XP-C58K,
      0483:070b) with a paper roll loaded

Safety:
    No paper is used. Nothing is queued for printing: every exchange is either an
    EP0 class request or a real-time status request, which the printer answers
    ahead of its print buffer. Use manual/printer_print to test printing.

Setup:
    1. Connect the host board to the PC.
    2. Connect the printer to the host board's USB host port and switch it on.
    3. Load a paper roll and close the cover - the test checks the paper sensor and
       fails if it reports empty.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/printer_escpos/printer_escpos.py -v -s
"""


def test_printer_escpos(dut):
    """
    Expected result (pass):  Sketch prints the device ID with its IEEE 1284 fields,
                             a port status with paper and no error, the four
                             real-time status bytes, 20/20 answered polls, a
                             successful SOFT_RESET with working endpoints after it,
                             and "[PASS]".
    Expected result (fail):  Printer not found, a class request failing, a device ID
                             that does not parse, paper reported empty (load a
                             roll), an unanswered status poll, endpoints dead after
                             SOFT_RESET, crash, or timeout.
    """
    dut.expect("printer_escpos test start")
    print("\nConnect an ESC/POS USB receipt printer with paper loaded.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=60) == b"[PASS]"
