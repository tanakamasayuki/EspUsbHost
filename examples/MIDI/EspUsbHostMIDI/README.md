# EspUsbHostMIDI

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

```
connected: vid=0499 pid=1066 product=USB MIDI Interface
midi cable=0 cin=0x09 status=0x90 data1=60 data2=100
midi cable=0 cin=0x08 status=0x80 data1=60 data2=0
```
