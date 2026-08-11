"""
Purpose:
    Verify the ESC/POS print data path end to end against a real printer: a whole
    receipt in one bulk transfer, Japanese text from the printer's font ROM
    (Shift-JIS, FS & / FS C), a CODE128 barcode, a QR code, and the auto cutter.
    Also verifies the printer still answers real-time status afterwards, which is
    where a data-toggle problem provoked by a long transfer would show up.

Why manual:
    Requires a physical printer, uses paper, and the result that matters - what is
    on the slip - can only be checked by looking at it. No peer device implements
    the printer class.

Required hardware:
    - ESP32-S3 host board
    - ESC/POS USB receipt printer with a Japanese font ROM and an auto cutter
      (developed against an Xprinter XP-C58K, 0483:070b), with a paper roll loaded

Safety / consumables:
    ONE SLIP PER RUN, about 10 cm, and the paper is cut. The test refuses to print
    if the printer reports paper out or an error beforehand, and fails if either
    appears afterwards - so a run that empties the roll fails rather than quietly
    printing nothing. Use manual/printer_escpos for a check that uses no paper.

Setup:
    1. Connect the host board to the PC.
    2. Connect the printer to the host board's USB host port and switch it on.
    3. Load a paper roll and close the cover.
    4. Set TEST_SERIAL_PORT_ESP32S3 in .env to the host board's serial port.
    5. Run: uv run --env-file .env pytest manual/printer_print/printer_print.py -v -s
    6. Look at the slip - see the checklist below.
"""


def test_printer_print(dut):
    """
    Expected result (pass):  Sketch prints a status before and after, "receipt sent",
                             5/5 answered status polls afterwards, and "[PASS]" - and
                             a slip comes out.

                             On the slip: the title in double-height Japanese, a
                             Japanese welcome line, three aligned item rows, a total
                             in bold, a CODE128 barcode with digits under it, a QR
                             code, a Japanese footer, and a clean partial cut. Kanji
                             coming out as pairs of unrelated symbols means the
                             printer's font ROM is not Shift-JIS; nothing else on the
                             slip is affected by that.
    Expected result (fail):  Printer not found, no status answer, paper out or an
                             error before or after printing, the bulk write failing,
                             the status path dead after the transfer, crash, or
                             timeout.
    """
    dut.expect("printer_print test start")
    print("\nConnect an ESC/POS USB receipt printer with paper loaded. One slip will be printed.")
    assert dut.expect_exact(["[PASS]", "[FAIL]"], timeout=90) == b"[PASS]"
