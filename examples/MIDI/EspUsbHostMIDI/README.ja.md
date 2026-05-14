# EspUsbHostMIDI

> English: [README.md](README.md)

USB MIDIの入出力サンプルです。接続したUSB MIDIデバイスからMIDIメッセージを受信し、シリアルコマンドでMIDIメッセージを送信します。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB MIDIデバイス（キーボード・コントローラー・インターフェースなど）

## 動作内容

- 受信したすべてのMIDIメッセージをシリアルに表示
- シリアルコマンドに応じてデバイスへ各種MIDIメッセージを送信

## シリアルコマンド

| コマンド | 送信されるMIDIメッセージ |
|----------|--------------------------|
| `n` | Note On — ch0、ノート60（C4）、ベロシティ100 |
| `f` | Note Off — ch0、ノート60（C4）、ベロシティ0 |
| `c` | Control Change — ch0、CC#74、値64 |
| `p` | Program Change — ch0、プログラム10 |
| `b` | Pitch Bend — ch0、値+1024 |
| `s` | SysEx — `F0 7D 01 02 F7` |

## 主要API

- `usb.onMidiMessage(callback)` — MIDIパケット受信ごとに`EspUsbHostMidiMessage`付きで呼ばれる
  - `message.cable` — USB MIDIケーブル番号
  - `message.codeIndex` — USB MIDIコードインデックス（CIN）
  - `message.status` — MIDIステータスバイト
  - `message.data1`, `message.data2` — MIDIデータバイト
- `usb.midiSendNoteOn(channel, note, velocity)`
- `usb.midiSendNoteOff(channel, note, velocity)`
- `usb.midiSendControlChange(channel, control, value)`
- `usb.midiSendProgramChange(channel, program)`
- `usb.midiSendPitchBendSigned(channel, value)` — 値の範囲：−8192〜+8191
- `usb.midiSendSysEx(data, length)`

## シリアル出力例

受信したMIDIメッセージはタイプごとにデコードして表示されます。ノート番号はノート名（例：`C4`・`D#3`）で表示されます。
ピッチベンドは −8192〜+8191 の符号付き整数で表示されます。

```
connected: vid=0499 pid=1066 product=USB MIDI Interface
Note On    ch=1 note=C4 vel=100
Note Off   ch=1 note=C4 vel=0
CC         ch=1 ctrl=74 val=64
PitchBend  ch=1 val=1024
```
