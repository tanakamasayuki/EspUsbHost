"""
Purpose:
    Work out which USB Printer Class (interface class 0x07) requests an ESC/POS
    printer really answers, after manual/printer_escpos found two suspicious
    results on an Xprinter XP-C58K:

      - GET_DEVICE_ID failed outright. wValue is the configuration index and
        wIndex packs the interface in the *high* byte, unlike the other requests in
        the class, so a host mistake looks exactly like an unimplemented request.
        The probe sweeps both fields plus the device-recipient and vendor-type
        forms some firmware wants.
      - GET_PORT_STATUS answered 0x00, which decodes as "deselected, error", while
        the real-time status said the printer was fine. The byte is read first,
        after other exchanges, and after SOFT_RESET, to see whether it is simply
        unimplemented or wakes up later.

    Reading it:
      - a variant that returns a length and an IEEE 1284 string -> the request works
        and the addressing is what that line says; fix the example to match.
      - every variant failing while DLE EOT answers -> the printer does not
        implement the request, and the class requests cannot be relied on. The
        example must fall back to real-time status, which is what the ESC/POS
        language provides for exactly this reason.

Why a probe:
    Exploratory, and the answer is in the log rather than in a pass/fail.

Safety:
    No paper is used. Nothing is queued for printing; every exchange is an EP0
    request or a real-time status request answered ahead of the print buffer.

Required hardware:
    - ESP32-S3 host board (TEST_SERIAL_PORT_ESP32S3)
    - ESC/POS USB receipt printer (developed against an Xprinter XP-C58K, 0483:070b)

Run:
    uv run --env-file .env pytest probe/printer_class/printer_class_probe.py -v -s
"""


def test_printer_class_probe(dut):
    """
    Expected result:  One line per request variant with its result, the four
                      real-time status bytes for comparison, and PROBE_DONE. Which
                      variants answered is the finding.
    """
    dut.expect("printer_class probe start")
    dut.expect_exact("PROBE_DONE", timeout=60)
