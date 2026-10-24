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
connected: device: address=1 portId=0x01 vid=0499 pid=1066 class=0x00(Device) speed=full product="USB MIDI Interface"
Note On    ch=1 note=C4 vel=100
Note Off   ch=1 note=C4 vel=0
CC         ch=1 ctrl=74 val=64
PitchBend  ch=1 val=1024
```

## 読み方と確認の手順

### 実行前の準備

USB MIDI入力を解釈し、明示的な操作で試験メッセージを送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`onMidiMessage()`で受信内容を表示し、UARTのn/fでNote On/Off、ほかの操作でcontrol・program・bend・SysExを送ります。音符の開始と終了を対にします。ESP32がメッセージを送ることと、楽器やソフトが音を出すことは別です。

### APIの使い方と改造時の注意

MIDI channelとUSB cable・portは別の指定です。APIのchannel番号の基準とpacketのfieldをホストアプリへ合わせます。MIDI event packetとUARTの生メッセージはbyte配置が異なり、MIDIは音声sampleではなく制御メッセージを運びます。

保持した音符には終了を送ります。送信結果を確認し、ホスト不在時の再試行・破棄を決めます。reset・切断で手元の発音状態が無効になる場合があります。複数化や完全なparserへ進む前に、一channel・一portで確認します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostMIDI.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
