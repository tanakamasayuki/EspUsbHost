"""
Purpose:
    Find the command sequence a Sony RC-S300 needs for a FeliCa Polling with an
    explicit System Code. CCID has no notion of a polling target, so this has to
    be a vendor command sequence; the probe determines the transport, the pseudo
    APDU dialect and the exact data objects the reader accepts.

Findings (this probe established the sequence the example now uses):
    - PC_to_RDR_Escape (0x6e) is not supported at all -- every payload fails. The
      pseudo APDUs go out over PC_to_RDR_XfrBlock (0x6f), i.e. ccidTransfer().
    - The pseudo APDU is FF 50 00 <P2> <Lc> <objects> 00, P2 selecting the object
      group: 00 manage session, 01 transparent exchange, 02 switch protocol. The
      standard PC/SC INS 0xC2 form is answered too.
    - The objects are PC/SC part 3's: 81 00 start session, 82 00 end, 83 00 RF off,
      84 00 RF on, 8F 02 <p p> switch protocol, 5F 46 04 <us LE> timeout,
      95 <len> <frame> transmit.
    - Answers are TLV: C0 03 <result> <SW1 SW2>, then response objects, then the
      pseudo APDU's own 90 00. result 00 / 9000 = accepted, 01 / 6301 = refused,
      01 / 6401 = malformed, 01 / 6700 = wrong object length, 01 / 6A81 =
      unsupported value, 02 / 6401 = sent but nothing answered.
    - The timeout object is mandatory in an exchange.
    - Switch protocol needs two value bytes (8F 01 <p> is refused with 6700), and
      03 00 is the value a FeliCa Polling is answered under. The reversed 00 03 is
      accepted too, and once answered with a response object 8F 01 08, but every
      Polling sent after it goes unanswered -- so the echo is not what it looks
      like, and 00 03 selects something else.
    - The field has to be cycled: RF off then RF on before the exchange. With RF on
      alone, a Suica whose IDm the reader's own path was reading fine at that very
      moment answered nothing.
    - The frame in the transmit object carries its own FeliCa length byte. Without
      it the target does not answer.
    - A successful exchange answers C0 03 00 9000, then 92 01 00, then
      96 02 00 00, then the target's frame in a 97 object.
    - No bit framing object is needed. 90 01 <x> is refused with 6700 and
      90 02 <x x> with 03 / 6401, and adding either breaks an exchange that works
      without it.

Why a probe:
    Exploratory. The sketch is a byte pump driven line by line from here, so the
    candidate sequences can be changed without reflashing.

Required hardware:
    - ESP32-S3 host board (TEST_SERIAL_PORT_ESP32S3)
    - Sony RC-S300 (VID 0x054c PID 0x0dc8) on the host port
    - A FeliCa card resting on the reader for the whole run

Run:
    uv run --env-file .env pytest probe/rcs300_felica/rcs300_felica_probe.py -v -s
"""

import re


START_SESSION = "ff 50 00 00 02 81 00 00"
END_SESSION = "ff 50 00 00 02 82 00 00"
RF_OFF = "ff 50 00 00 02 83 00 00"
RF_ON = "ff 50 00 00 02 84 00 00"

GET_UID = "ff ca 00 00 00"

# 4 byte little endian microseconds.
TIMEOUT_1S = "5f 46 04 40 42 0f 00"

# What an empty field, or a frame no target understood, answers with.
NO_ANSWER = "c0030264019000"


def _hex(data):
    return " ".join(f"{b:02x}" for b in data)


def _apdu(p2, body):
    body = bytes.fromhex(body.replace(" ", ""))
    return f"ff 50 00 {p2:02x} {len(body):02x} {_hex(body)} 00"


def _polling(system_code, with_length=True, request_code=0x01):
    frame = bytes([0x00, system_code >> 8, system_code & 0xFF, request_code, 0x00])
    return bytes([len(frame) + 1]) + frame if with_length else frame


class Probe:
    def __init__(self, dut):
        self.dut = dut

    def send(self, command, label="", quiet=False):
        if not re.match(r"^[?spft](\s|$)", command):
            command = "x " + command
        self.dut.write(command + "\n")
        answer = self.dut.expect(re.compile(rb"RSP [^\r\n]*\r?\n"), timeout=20).group(0).decode().strip()
        if not quiet:
            print(f"  {label or command:34} -> {answer}")
        return answer


def test_rcs300_felica_probe(dut):
    """
    Expected result:  Every variant is logged with the reader's answer. Anything
                      other than 02 / 6401 on an exchange is the finding.
    """
    dut.expect_exact("TEST_BEGIN rcs300_felica_probe")
    dut.expect_exact("PROBE_READY", timeout=30)
    probe = Probe(dut)
    probe.send("t 8000", "set timeout")

    # Confirm through the reader's own path that the card on the reader is a
    # FeliCa one, so a failure below cannot be blamed on an empty field.
    print("\n--- reader-side card ---")
    probe.send("p", "power on")
    probe.send(GET_UID, "get uid")
    probe.send("f", "power off")

    # A bit framing object was the suspected missing piece: a FeliCa target listens
    # at 212 kbps, so a field still framing for Type A would look exactly like
    # 02 / 6401. It turned out not to be needed -- every candidate below is refused
    # and breaks an exchange that works without one -- and the real missing pieces
    # were the switch protocol value and cycling the field.
    framings = {
        "none": None,
        "90 01 00": "90 01 00",
        "90 01 03": "90 01 03",
        "90 02 00 03": "90 02 00 03",
    }
    switches = {
        "none": None,
        "8f 02 00 03": "8f 02 00 03",
        "8f 02 03 00": "8f 02 03 00",
    }

    print("\n--- sweep: switch value x position x frame framing x LEN byte ---")
    hits = []
    for switch_name, switch in switches.items():
        for position in ("before RF on", "after RF on"):
            if switch is None and position == "after RF on":
                continue
            for framing_name, framing in framings.items():
                for with_length in (True, False):
                    label = (f"sw={switch_name} {position} framing={framing_name} "
                             f"len={'y' if with_length else 'n'}")

                    probe.send(START_SESSION, quiet=True)
                    if switch is not None and position == "before RF on":
                        probe.send(_apdu(0x02, switch), quiet=True)
                    # A fresh field so the card powers up cleanly rather than
                    # staying in whatever state the reader's own polling left it.
                    probe.send(RF_OFF, quiet=True)
                    probe.send(RF_ON, quiet=True)
                    if switch is not None and position == "after RF on":
                        probe.send(_apdu(0x02, switch), quiet=True)

                    frame = _polling(0xFFFF, with_length=with_length)
                    body = TIMEOUT_1S
                    if framing is not None:
                        body += " " + framing
                    body += f" 95 {len(frame):02x} {_hex(frame)}"
                    answer = probe.send(_apdu(0x01, body), quiet=True)

                    probe.send(RF_OFF, quiet=True)
                    probe.send(END_SESSION, quiet=True)

                    if NO_ANSWER not in answer:
                        print(f"  HIT {label}\n      {answer}")
                        hits.append((label, answer))

    print(f"\n--- {len(hits)} hit(s) in the sweep ---")
    for label, answer in hits:
        print(f"  {label} -> {answer}")

    # If 95 only transmits, the answer has to be fetched separately: an exchange
    # that carries a timer and nothing else, and one that names a receive object.
    print("\n--- transmit then fetch separately ---")
    probe.send(START_SESSION, "start session")
    probe.send(_apdu(0x02, "8f 02 03 00"), "switch FeliCa")
    probe.send(RF_OFF, "RF off")
    probe.send(RF_ON, "RF on")
    frame = _polling(0xFFFF)
    probe.send(_apdu(0x01, f"{TIMEOUT_1S} 95 {len(frame):02x} {_hex(frame)}"), "transmit polling")
    probe.send(_apdu(0x01, TIMEOUT_1S), "fetch: timer only")
    probe.send(_apdu(0x01, f"{TIMEOUT_1S} 97 00"), "fetch: receive object")
    probe.send(_apdu(0x01, "97 00"), "fetch: receive object alone")
    probe.send(RF_OFF, "RF off")
    probe.send(END_SESSION, "end session")

    # And the reader's own path once more: if it still finds the card, all the RF
    # toggling left the reader healthy and the card in place.
    print("\n--- reader-side card again ---")
    probe.send("p", "power on")
    probe.send(GET_UID, "get uid")
    probe.send("f", "power off")
