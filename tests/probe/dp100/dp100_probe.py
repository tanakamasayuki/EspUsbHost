"""
Purpose:
    Establish the ALIENTEK DP100 (ATK-MDP100, 2e3c:af01) HID frame layout against
    real hardware: which CRC variant the device answers, where the response fields
    sit, and what the read-only opcodes return. No public first-party protocol
    document exists - only reverse-engineered projects - so the device itself is
    the reference.

Findings (this probe established what the example now implements):
    - The frame is [dir][opcode][reserved][len][data...][crc lo][crc hi], zero
      padded to the 64-byte report. Host->device dir 0xfb, device->host 0xfa.
    - The CRC is CRC-16/MODBUS over byte 0 through the last data byte, appended
      little endian. Only that variant is answered with a payload.
    - A rejected frame is still answered: opcode echoed, len 1, body 0x00. All
      three wrong CRC variants produced exactly `fa 10 00 01 00` + crc, so a
      one-byte 0x00 body is the device saying no rather than silence.
    - A read request carries no data: len 0 works for BASIC_INFO, and a 1-byte
      payload changes nothing about the answer.
    - DEVICE_INFO (0x10) answers 40 bytes, matching the layout the public
      reverse-engineering projects describe, field for field:
        16 B device type, "ATK-DP100\0" then 0xff padding -- note it is not the
                          USB product string, which is "ATK-MDP100"
         2 B hdw_ver = 14      2 B app_ver = 14
         2 B boot_ver = 11     2 B run_area = 0x00aa
        12 B serial = c7819d0000400416 22a75005 -- not the USB serial string
                      ("16A1C1C74000"), so one is derived from the other
         2 B year = 2024       1 B month = 12   1 B day = 2
    - BASIC_INFO (0x30) answers 16 bytes, and repeating it identifies the live
      values and their scale (measured with the output off and unloaded):
        vin    = 12160  -> mV, matches a ~12.16 V supply on the input
        vout   = 0         mV, output off
        iout   = 0         mA (scale not confirmed while the output is off)
        vo_max = 11800  -> mV, the ceiling the present input allows
        temp1  = 298    -> 0.1 degC = 29.8
        temp2  = 292    -> 0.1 degC = 29.2
        dc_5v  = 5067   -> mV, the internal 5 V rail
        out_mode = 2, work_st = 0 (one byte each)
      vin, the temperatures and dc_5v drift between reads; the rest hold still.
    - SYSTEM_INFO (0x40) answers 8 bytes: 50 00 1a 04 02 02 01 00. Field meanings
      are not established, so the example exposes it as raw bytes.
    - BASIC_SET (0x35) is NOT probed here. It carries the setpoints and the output
      enable, so guessing at its request form can switch a bench supply's output
      on. It needs a deliberate run with the terminals known to be unloaded.

Safety:
    Read-only. Only the opcodes that report state are sent; nothing writes a
    setting or switches the output. Keep the output terminals unloaded anyway.

Why a probe:
    Exploratory. The sketch is a byte pump driven line by line from here, so
    candidate frames can be changed without reflashing.

Required hardware:
    - ESP32-S3 host board (TEST_SERIAL_PORT_ESP32S3)
    - ALIENTEK DP100 connected directly to the host port, not through a hub

Run:
    uv run --env-file .env pytest probe/dp100/dp100_probe.py -v -s
"""

import re

# Read-only opcodes. The write side (BASIC_SET 0x35, SYSTEM_SET 0x45) and the
# firmware update opcodes are deliberately absent.
OP_DEVICE_INFO = 0x10
OP_BASIC_INFO = 0x30
OP_SYSTEM_INFO = 0x40

CRC_VARIANTS = {
    0: "MODBUS little endian",
    1: "MODBUS big endian",
    2: "MODBUS skipping the direction byte",
    3: "no CRC (zeros)",
}


def _crc16_modbus(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class Probe:
    def __init__(self, dut):
        self.dut = dut

    def send(self, command, label="", quiet=False):
        self.dut.write(command + "\n")
        answer = (
            self.dut.expect(re.compile(rb"RSP [^\r\n]*\r?\n"), timeout=20).group(0).decode().strip()
        )
        if not quiet:
            print(f"  {label or command:32} -> {answer}")
        return answer

    def opcode(self, opcode, data="", label=""):
        command = f"o {opcode:02x}" + (f" {data}" if data else "")
        return self.send(command, label or command)


def _payload(answer):
    """Returns the response frame bytes, or None when nothing answered."""
    match = re.search(r"data=([0-9a-f]+)", answer)
    if not match:
        return None
    return bytes.fromhex(match.group(1))


def _describe(frame):
    if frame is None:
        return "no answer"
    length = frame[3] if len(frame) > 3 else 0
    body = frame[4 : 4 + length]
    crc = frame[4 + length : 6 + length]
    expected = _crc16_modbus(frame[: 4 + length])
    crc_note = ""
    if len(crc) == 2:
        value = crc[0] | (crc[1] << 8)
        crc_note = f" crc={value:04x} expected_le={expected:04x} {'MATCH' if value == expected else 'differs'}"
    return (
        f"dir=0x{frame[0]:02x} op=0x{frame[1]:02x} reserved=0x{frame[2]:02x} "
        f"len={length}{crc_note}\n      body={body.hex()}"
    )


def test_dp100_probe(dut):
    """
    Expected result:  Every variant is logged with the device's answer. The CRC
                      variant that gets an answer, and the field offsets in that
                      answer, are the findings.
    """
    dut.expect_exact("TEST_BEGIN dp100_probe")
    dut.expect_exact("PROBE_READY", timeout=30)
    probe = Probe(dut)
    probe.send("t 1000", "set timeout")
    probe.send("?", "hid interface")

    # 1. Which CRC does the device accept? DEVICE_INFO is read-only and should be
    #    answerable at any time, so an answer here identifies the variant and
    #    confirms the frame layout at the same time.
    print("\n--- CRC variant sweep on DEVICE_INFO (0x10) ---")
    answered = []
    for variant, name in CRC_VARIANTS.items():
        probe.send(f"v {variant}", f"crc variant {variant} ({name})")
        answer = probe.opcode(OP_DEVICE_INFO, label=f"DEVICE_INFO v{variant}")
        frame = _payload(answer)
        print(f"      {_describe(frame)}")
        if frame is not None:
            answered.append(variant)

    if not answered:
        # Nothing answered: either the direction byte is different or the device
        # needs something before it will talk.
        print("\n--- no answer yet: direction byte sweep (MODBUS LE) ---")
        probe.send("v 0", "crc variant 0")
        for direction in ("fb", "fa", "aa", "55", "00"):
            probe.send(f"d {direction}", f"direction {direction}")
            answer = probe.opcode(OP_DEVICE_INFO, label=f"DEVICE_INFO dir={direction}")
            print(f"      {_describe(_payload(answer))}")
        probe.send("d fb", "direction back to fb")
        print("\nNo frame was answered. Next step is a report descriptor dump "
              "(manual/hid_report_descriptor) and a look at whether the device "
              "expects a connect frame first.")
        return

    variant = answered[0]
    print(f"\n=> answered with CRC variant {variant} ({CRC_VARIANTS[variant]})")
    probe.send(f"v {variant}", "lock the variant in")

    # 2. Does the request length matter? A read may want Len=0 or a selector byte.
    print("\n--- request payload shapes on BASIC_INFO (0x30) ---")
    for data, label in (("", "len=0"), ("00", "len=1 data=00"), ("01", "len=1 data=01")):
        answer = probe.opcode(OP_BASIC_INFO, data, label=f"BASIC_INFO {label}")
        print(f"      {_describe(_payload(answer))}")

    # 3. DEVICE_INFO body, matched against the USB descriptor strings. The product
    #    string is "ATK-MDP100" and the serial "16A1C1C74000", so finding those in
    #    the body pins the payload offsets without guessing.
    print("\n--- DEVICE_INFO body vs the descriptor strings ---")
    frame = _payload(probe.opcode(OP_DEVICE_INFO, label="DEVICE_INFO"))
    if frame is not None:
        body = frame[4 : 4 + frame[3]]
        printable = "".join(chr(b) if 32 <= b < 127 else "." for b in body)
        print(f"      body ascii: {printable}")
        for needle in ("ATK-MDP100", "MDP100", "DP100"):
            index = printable.find(needle)
            print(f"      {needle!r} at offset {index}")

    # 4. SYSTEM_INFO, also read-only.
    print("\n--- SYSTEM_INFO (0x40) ---")
    print(f"      {_describe(_payload(probe.opcode(OP_SYSTEM_INFO, label='SYSTEM_INFO')))}")

    # 5. Repeat BASIC_INFO: the values that move between reads are the live
    #    measurements, which is what identifies them and their scale.
    print("\n--- BASIC_INFO repeated (output off, unloaded) ---")
    for i in range(5):
        frame = _payload(probe.opcode(OP_BASIC_INFO, label=f"BASIC_INFO #{i}"))
        if frame is None:
            continue
        body = frame[4 : 4 + frame[3]]
        words = [int.from_bytes(body[n : n + 2], "little") for n in range(0, len(body) - 1, 2)]
        print(f"      u16le: {words}")
