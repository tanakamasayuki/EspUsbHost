# EspUsbHostMIDI

> 日本語版: [README.ja.md](README.ja.md)

Demonstrates USB MIDI input and output: receives MIDI messages from a connected USB MIDI device and sends MIDI commands via Serial commands.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB MIDI device (keyboard, controller, interface, etc.)

## What it does

- Receives all incoming MIDI messages and prints them to Serial
- Sends various MIDI messages to the device on Serial command

## Serial commands

| Command | MIDI message sent |
|---------|-------------------|
| `n` | Note On — channel 0, note 60 (C4), velocity 100 |
| `f` | Note Off — channel 0, note 60 (C4), velocity 0 |
| `c` | Control Change — channel 0, CC#74, value 64 |
| `p` | Program Change — channel 0, program 10 |
| `b` | Pitch Bend — channel 0, value +1024 |
| `s` | SysEx — `F0 7D 01 02 F7` |

## Key APIs

- `usb.onMidiMessage(callback)` — fired on each incoming MIDI packet with `EspUsbHostMidiMessage`
  - `message.cable` — USB MIDI cable number
  - `message.codeIndex` — USB MIDI code index number (CIN)
  - `message.status` — MIDI status byte
  - `message.data1`, `message.data2` — MIDI data bytes
- `usb.midiSendNoteOn(channel, note, velocity)`
- `usb.midiSendNoteOff(channel, note, velocity)`
- `usb.midiSendControlChange(channel, control, value)`
- `usb.midiSendProgramChange(channel, program)`
- `usb.midiSendPitchBendSigned(channel, value)` — value range −8192 to +8191
- `usb.midiSendSysEx(data, length)`

## Expected Serial output

Incoming MIDI messages are decoded by type. Note numbers are shown as note names (e.g. `C4`, `D#3`).
Pitch bend values are shown as signed integers in the range −8192 to +8191.

```
connected: device: address=1 portId=0x01 vid=0499 pid=1066 class=0x00(Device) speed=full product="USB MIDI Interface"
Note On    ch=1 note=C4 vel=100
Note Off   ch=1 note=C4 vel=0
CC         ch=1 ctrl=74 val=64
PitchBend  ch=1 val=1024
```

## Walkthrough and checks

### Before running

Decode USB MIDI input and send explicit test messages. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onMidiMessage()` prints received MIDI meaning. UART n/f send Note On/Off and the other documented commands send control, program, bend and SysEx data. Pair note activation with release; the ESP32 sends messages, but sound requires an instrument or software that acts on them.

### API use and adaptation

A MIDI channel and a USB cable/port are different selectors. Match the API's channel indexing and packet fields to your host application; a MIDI event packet is not the same byte layout as a raw UART message. MIDI transfers control messages, not audio samples.

Always provide the matching release for held notes. Check send results and decide whether to retry or discard when the host is absent. Reset and disconnect can invalidate local note state. Begin with one channel and one port before adding multiplexing or a full MIDI parser.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostMIDI.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
