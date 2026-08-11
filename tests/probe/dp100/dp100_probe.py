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
    - BASIC_SET (0x35) is established by the second test in this file, which does
      switch the output on. In short: read with `35 <index|0x80>`, write ten bytes
      with `35 <index|0x20> <state> <vo> <io> <ovp> <ocp>`, and the state byte is
      the output enable. See its own docstring.

Safety:
    The first test is read-only. The second one writes setpoints and switches the
    output on, and says so in its own docstring: run it only with nothing connected
    to the DP100's output terminals.

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

OP_DEVICE_INFO = 0x10
OP_BASIC_INFO = 0x30
# Only the second test sends this one: it carries the setpoints and the output
# enable. SYSTEM_SET (0x45) and the firmware update opcodes are absent on purpose.
OP_BASIC_SET = 0x35
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

    def opcode(self, opcode, data="", label="", quiet=False):
        command = f"o {opcode:02x}" + (f" {data}" if data else "")
        return self.send(command, label or command, quiet=quiet)


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


def test_dp100_setpoint_probe(dut):
    """
    Establishes the BASIC_SET (0x35) request form: how a setpoint is read back, what
    the index selects, and which state byte switches the output on.

    THIS TEST SWITCHES THE SUPPLY'S OUTPUT ON. Run it only with nothing connected
    to the DP100's output terminals. It uses 5.000 V / 0.500 A and finishes by
    switching the output off and writing the original setpoint back.

    The output state is never taken on trust from the answer to a write: after every
    candidate the probe reads BASIC_INFO (0x30) and looks at vout, which is what the
    supply is actually doing.

    Findings (this probe established what the example now implements):
        - A one-byte answer body is a status code, not just a refusal: 0x00 is
          failure (what a wrong CRC gets) and 0x01 is success (what an accepted
          BASIC_SET gets).
        - BASIC_SET is read by index: `35` with a one-byte payload holding the
          index. Len 0 is NOT answered as a setpoint - the device replies with a
          BASIC_INFO frame instead, which is why every answer here is matched on
          its opcode before being read.
        - The indices are preset groups. As found: index 0 = 4000 mV / 3000 mA,
          index 1 = 2000 mV / 1000 mA, index 2 = 3000 mV / 1500 mA,
          index 3 = 4000 mV / 2000 mA. All carry ovp 30500 mV / ocp 5050 mA,
          matching the 30 V / 5 A rating.
        - THE WRITE FLAG IS 0x20 IN THE INDEX, and getting it wrong is silent: a
          write to a bare index, or to index | 0x80, or to index | 0xa0, is
          answered with status 0x01 and then completely ignored. Nothing about the
          answer distinguishes it from a write that took, which is why every write
          in this file is followed by a readback. `index | 0x20` is what makes the
          setpoint move.
          (0x20 as the write flag is also what scottbez1/webdp100, Apache-2.0,
          does -- with the comment that it is "presumably a bit mask, but not sure
          what it means". Three rounds of sweeping index and state bytes here found
          no effect before that was checked.)
        - The read flag is 0x80. A bare index answers too, but reports the stored
          preset: index 0 read bare came back with state 0x01 while `0x80` reported
          state 0x00 for the same values.
        - The state byte is the output enable, and only hardware could confirm it:
          with index | 0x20 and the setpoint at 5000 mV, state 0x01 measured
          vout = 5000 mV on BASIC_INFO and state 0x00 took it back to 0.
        - A setpoint write does not switch the output on by itself, and ovp / ocp
          are carried through unchanged when the frame repeats them.
    """
    import time

    dut.expect_exact("TEST_BEGIN dp100_probe")
    dut.expect_exact("PROBE_READY", timeout=30)
    probe = Probe(dut)
    probe.send("t 1000", "set timeout")
    probe.send("v 0", "crc variant 0")

    def frame_for(opcode, data="", label="", quiet=False):
        """Sends a request and returns the answer only if it carries `opcode`.

        The probe sketch hands back whatever report arrived next, and this device
        answers some requests with a different opcode's frame, so matching here is
        what keeps a BASIC_INFO from being read as a setpoint.
        """
        frame = _payload(probe.opcode(opcode, data, label=label, quiet=quiet))
        if frame is None or len(frame) < 4 or frame[1] != opcode:
            return None
        return frame

    def body_of(frame):
        return frame[4 : 4 + frame[3]] if frame else b""

    def status_of(frame):
        """A one-byte body: 0x01 accepted, 0x00 rejected."""
        body = body_of(frame)
        return body[0] if len(body) == 1 else None

    def output_state(label=None):
        """(vout mV, iout mA, work_st) as the supply reports it right now."""
        frame = frame_for(OP_BASIC_INFO, label="BASIC_INFO", quiet=True)
        body = body_of(frame)
        if len(body) < 16:
            print(f"      {label}: BASIC_INFO failed")
            return None
        state = (
            int.from_bytes(body[2:4], "little"),
            int.from_bytes(body[4:6], "little"),
            body[15],
        )
        if label:
            print(f"      {label}: vout={state[0]} mV iout={state[1]} mA work_st={state[2]}")
        return state

    def read_setpoint(index, label=None, quiet=False):
        frame = frame_for(OP_BASIC_SET, f"{index:02x}", label=label or f"BASIC_SET read index={index}",
                          quiet=quiet)
        body = body_of(frame)
        if len(body) < 10:
            return None
        return {
            "index": body[0],
            "state": body[1],
            "vo_set": int.from_bytes(body[2:4], "little"),
            "io_set": int.from_bytes(body[4:6], "little"),
            "ovp": int.from_bytes(body[6:8], "little"),
            "ocp": int.from_bytes(body[8:10], "little"),
        }

    def write_setpoint(setpoint, label):
        payload = bytes([setpoint["index"], setpoint["state"]]) + b"".join(
            int(setpoint[key]).to_bytes(2, "little") for key in ("vo_set", "io_set", "ovp", "ocp")
        )
        frame = frame_for(OP_BASIC_SET, payload.hex(), label=label)
        status = status_of(frame)
        print(f"      status={status} ({'accepted' if status == 1 else 'rejected'})")
        return status == 1

    # 1. Which request form returns a setpoint, and what do the indices hold?
    print("\n--- reading setpoints by index ---")
    print(f"  len=0 answers opcode "
          f"0x{(_payload(probe.opcode(OP_BASIC_SET, label='BASIC_SET len=0')) or bytes(2))[1]:02x}"
          " (not 0x35, so len 0 is not a setpoint read)")
    groups = {}
    for index in range(4):
        setpoint = read_setpoint(index)
        if setpoint:
            groups[index] = setpoint
            print(f"      {setpoint}")
        else:
            print(f"      index={index}: no setpoint answered")

    if 0 not in groups:
        print("\nIndex 0 did not answer, so the write sweep below would be guesswork.")
        return

    original = dict(groups[0])
    print(f"\n=> setpoints are read as `35 <index>`; index 0 = {original}")
    output_state("before any write")

    # 2. A write with the output left alone: same state byte, new setpoint, read it
    #    back. This separates "the write frame is right" from "the output enable bit
    #    is right".
    print("\n--- write the setpoint, keeping the state byte as found ---")
    candidate = dict(original, vo_set=5000, io_set=500)
    write_setpoint(candidate, "BASIC_SET vo=5000 io=500")
    readback = read_setpoint(0, label="BASIC_SET read back")
    print(f"      {readback}")
    if readback and (readback["vo_set"], readback["io_set"]) == (5000, 500):
        print("      => the setpoint write and its readback are confirmed")
    else:
        print("      => the setpoint did not take; the index or field order is wrong")
    output_state("after the setpoint write")

    # 3. The index byte carries flag bits, and the write flag is 0x20 - not the
    #    0x80 / 0xa0 the protocol notes suggest. A write to a bare index is
    #    accepted (status 1) and then silently ignored, which is what sent this
    #    looking for the flag in the first place.
    print("\n--- reading with index | 0x80 ---")
    print(f"      {read_setpoint(0x80, label='BASIC_SET read index=0x80')}")

    print("\n--- write flag sweep: index | 0x20 vs a bare index (5.000 V / 0.500 A) ---")
    base = dict(original, vo_set=5000, io_set=500)
    for index in (0x00, 0x20):
        write_setpoint(dict(base, index=index, state=0x00),
                       f"BASIC_SET index=0x{index:02x} state=0x00")
        time.sleep(0.4)
        back = read_setpoint(0x80, quiet=True)
        print(f"      live setpoint now: {back}")
        if back and back["vo_set"] == 5000:
            print(f"      => index 0x{index:02x} is the write form")
        output_state(f"after index=0x{index:02x} state=0x00")

    # 4. With the setpoint proven, the state byte is the output enable.
    print("\n--- output enable: state byte with the write flag ---")
    for state in (0x01, 0x00):
        write_setpoint(dict(base, index=0x20, state=state),
                       f"BASIC_SET index=0x20 state=0x{state:02x}")
        time.sleep(0.6)
        output_state(f"after state=0x{state:02x}")

    # 5. Leave the supply off and back at the setpoint it was found with.
    print("\n--- restore ---")
    write_setpoint(dict(original, index=0x20, state=0x00), "restore, output off")
    time.sleep(0.5)
    final = output_state("after restore")
    print(f"      live now: {read_setpoint(0x80, quiet=True)}")
    assert final is not None and final[0] < 500, (
        f"the output is still on ({final}); switch it off from the front panel")
