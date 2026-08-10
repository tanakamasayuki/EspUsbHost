# EspUsbHostCcidFelicaIdm

> 日本語版: [README.ja.md](README.ja.md)

Reads a FeliCa IDm with a System Code of your choosing, through a Sony RC-S300.

## Why this is not `ccidPowerOn()` + Get UID

A contactless CCID reader polls the field on its own and presents whatever
answered as a card in its slot. CCID has no notion of a polling target — no
message in the specification says what to poll for — so a host gets whatever the
reader's own wildcard poll found and cannot ask for anything else.

That is fine for a card with one system on it. It is not fine for a phone. What
the reader's own polling found on an iPhone with a Suica in its wallet, and what
this example found on the same phone a moment later:

| Path | Answer |
|------|--------|
| The reader's own polling: `ccidPowerOn()` + Get UID | An ATR with no historical bytes and a 4-byte NFCID1 beginning `0x08` — a random ISO 14443 Type A id. The wallet answered as Type A, and the Suica was never in the picture |
| This example, FeliCa Polling with System Code `0xffff` | An 8-byte IDm, answering System Code `0x0003` — the Suica |
| This example, FeliCa Polling with System Code `0x0003` | The same IDm |

So what reaches the transit card is polling FeliCa at all, which a CCID host
cannot ask its reader to do. The System Code is what then pins down which system
the IDm belongs to: `0xffff` takes whatever the target puts first and only says so
in its answer, while a concrete code either reaches that system or nothing.

This example takes the RF field over from the reader and sends both:

```
System Code 0xffff  wildcard, whatever the target puts first
System Code 0x0003  the system the JR-compatible transit cards live in
```

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- **Sony RC-S300** (`FeliCa Port/PaSoRi 4.0`, VID 0x054c PID 0x0dc8). The command
  set is Sony's, not CCID's — see [Portability](#portability)
- A FeliCa target: a transit card (Suica, PASMO, ...), any other FeliCa card, or a
  phone with a transit card in its wallet

## What it does

- Opens the reader's CCID interface (`ccidOpen()`) and refuses to go on unless the
  device is an RC-S300
- Opens a transparent session, switches the field to FeliCa, and cycles the field
  off and on so the target powers up cleanly
- Sends a FeliCa Polling for System Code `0xffff` and then for `0x0003`, printing
  the IDm, the PMm and the System Code the target answered from
- Turns the field off and closes the session, so the reader can poll on its own again

```
connected: address=2 vid=054c pid=0dc8 product="FeliCa Port/PaSoRi 4.0"
RC-S300 ready: address=2
wildcard SC=ffff IDm=0114b5f2c3d4e5f6  PMm=00f0000000010b4b  answering system code=0003
transit  SC=0003 IDm=0114b5f2c3d4e5f6  PMm=00f0000000010b4b  answering system code=0003
```

## How it works

Three layers, split so that everything that can be tested without hardware is:

| File | Contents |
|------|----------|
| [`FelicaProtocol.hpp`](FelicaProtocol.hpp) | FeliCa Polling framing (JIS X 6319-4). Pure byte formatting |
| [`Rcs300Protocol.hpp`](Rcs300Protocol.hpp) | The RC-S300's transparent session pseudo APDUs and its response objects. Pure byte formatting |
| [`Rcs300Device.hpp`](Rcs300Device.hpp) | The USB half: each pseudo APDU sent as a `PC_to_RDR_XfrBlock` through `ccidTransfer()` |

The two protocol headers have no Arduino or USB dependencies, so
[`tests/unit/felica_idm`](../../../tests/unit/felica_idm/) compiles them with g++
and checks every frame byte for byte. The hardware end is
[`tests/manual/ccid_felica`](../../../tests/manual/ccid_felica/).

### The transparent session

Nothing was added to the library for this. A transparent session is a
reader-specific protocol, so it sits in this example on top of the raw CCID
message APIs the library already has — the split the library's CCID design
[set out](../../../docs/ccid-api-spec.ja.md) from the start.

Measured against a real RC-S300 by
[`tests/probe/rcs300_felica`](../../../tests/probe/rcs300_felica/):

- `PC_to_RDR_Escape` is **not supported at all** — every payload fails. The pseudo
  APDUs go out over `PC_to_RDR_XfrBlock`, i.e. `ccidTransfer()`
- The pseudo APDU is `FF 50 00 <P2> <Lc> <data objects> 00`, where P2 selects the
  object group: `00` manage session, `01` transparent exchange, `02` switch
  protocol. The reader answers the standard PC/SC `FF C2` form too
- The data objects are the ones PC/SC part 3 defines for a transparent session:

  | Object | Meaning |
  |--------|---------|
  | `81 00` | start transparent session |
  | `82 00` | end transparent session |
  | `83 00` | RF off |
  | `84 00` | RF on |
  | `8F 02 03 00` | switch the field to FeliCa. Accepted with no response object. The reversed `8F 02 00 03` is accepted too — and once answered `8F 01 08` — but no Polling sent after it is ever answered, so it selects something else |
  | `5F 46 04 <µs LE>` | how long to wait for the target's answer. **Mandatory** in an exchange |
  | `95 <len> <frame>` | the frame to send |

- Every answer starts with a status object `C0 03 <result> <SW1 SW2>`, then any
  response objects, then the pseudo APDU's own status word:

  | result / SW | Meaning |
  |-------------|---------|
  | `00` / `9000` | accepted |
  | `01` / `6301` | refused (e.g. RF on outside a session) |
  | `01` / `6401` | malformed request (e.g. an exchange with no timeout object) |
  | `01` / `6700` | wrong object length (e.g. `8F 01 <p>` instead of `8F 02 <p p>`) |
  | `01` / `6A81` | unsupported value (e.g. a switch protocol value the reader has no mode for) |
  | `02` / `6401` | sent, but nothing in the field answered |

- A successful exchange answers with the status object, then `92 01 00` and
  `96 02 00 00`, then the target's frame in a `97` object:

  ```
  C0 03 00 9000 | 92 01 00 | 96 02 0000 | 97 14 <20 byte Polling answer> | 9000
  ```

- **The field has to be cycled off and on before the exchange.** The reader has
  been polling on its own, and a Polling sent into the field it left behind goes
  unanswered — measured with a Suica whose IDm the reader's own path was reading
  fine at the same moment. `RF off` then `RF on` fixes it.
- The frame in the transmit object carries its own FeliCa length byte. Without it
  the target does not answer.

### FeliCa Polling

`LEN 00 <SC1> <SC2> <RC> <TSN>`, where LEN counts itself, and the answer is
`LEN 01 <IDm 8> <PMm 8> [<request data 2>]`. Request Code `0x01` makes the target
report the System Code it answered from, which is what makes a wildcard poll's
answer self-describing: it names the system that was reached.

## Portability

Verified on a Sony RC-S300 only, and deliberately restricted to it: `open()`
checks VID/PID and refuses anything else. `FelicaProtocol.hpp` is not
reader-specific and takes any System Code, but the transparent session in
`Rcs300Protocol.hpp` is Sony's dialect. Another reader needs its own device layer
even where it implements the same PC/SC objects.

## References

- USB Device Class Specification for Integrated Circuit Card Devices (CCID)
- PC/SC Workgroup specification part 3, transparent session and its data objects
- JIS X 6319-4 (FeliCa) for the Polling command and its answer
- [`docs/ccid-api-spec.ja.md`](../../../docs/ccid-api-spec.ja.md) — the library's
  CCID design, including why reader-specific protocols stay out of it

FeliCa, PaSoRi and Suica are trademarks of their respective owners. This example
is not affiliated with, or endorsed by, Sony or any transit operator; it only
speaks the documented protocols to a reader.
