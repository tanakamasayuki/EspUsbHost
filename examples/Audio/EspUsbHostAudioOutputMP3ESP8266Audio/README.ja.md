# EspUsbHostAudioOutputMP3ESP8266Audio

> English: [README.md](README.md)

[ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)で埋め込みMP3ファイルをデコードし、USBスピーカーなどのUSB Audio出力デバイスで順番に再生するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 依存ライブラリ

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)（Arduino Library Managerで入手可能、GPL-3.0 License）— MP3デコード

## 動作内容

- MP3ファイルを`assets_embed.h`でCの配列として埋め込み、順番に再生
- `AudioGeneratorMP3`で各MP3をデコードし、`RingBufferOutput`経由でデコード済みPCMをリングバッファへ書き込む
- `RingBufferOutput::ConsumeSample()`内の線形補間リサンプラーで、MP3のソースレートから48 kHzへ変換
- `onAudioOutputRequest()`でバッファ済みPCMを`request.data`へコピーし、`request.writtenFrames`を設定
- USB Audio OUTの送信タイミングはEspUsbHostに任せ、デコードは`loop()`で進める

## 注意

- MP3ファイルは `assets_embed.h` でCの配列として埋め込まれており、順番に再生されます。MP3以外のファイルはスキップされます。
- デコーダが `SetRate()` でMP3のサンプルレートを通知します。線形補間リサンプラーによってUSB出力レート（48 kHz）に変換されます。
- USB出力は48 kHz・16-bit ステレオPCMです。接続するUSBデバイスがこのフォーマットに対応している必要があります。
- `onAudioOutputRequest()`で要求されたフレーム数をリングバッファから取り出し、送信タイミングはライブラリに任せます。
- 全ファイルを1周再生したら停止します。
- ESP8266AudioはMP3以外にも複数形式に対応する多機能ライブラリです。一方でGPL-3.0ライセンスのため、配布するプロジェクトで使う場合はライセンス条件を確認してください。
- ESP8266Audioは出力機能も持っていますが、このサンプルではUSB Audio OUTをEspUsbHostが管理するため、ESP8266Audioは主にコーデック部分だけを利用します。そのためリングバッファやリサンプリングなどの接続コードが必要です。
- 対応コーデックが多い分、PCMFlow版よりビルドに時間がかかることがあります。

## 主要API

- `usb.onAudioOutputRequest(callback)` — USB転送が次のPCMフレームを必要とするたびに呼ばれる
  - `request.data` — インターリーブされたPCMサンプルを書き込むバッファ
  - `request.frameCount` — 要求されたフレーム数
  - `request.writtenFrames` — 実際に書き込んだフレーム数を設定する
  - `request.channels` — 選択したストリームのチャンネル数
- `usb.audioOutputReady(address)` — USB Audio OUT endpointが見つかっているときtrue
- `usb.getAudioStreams(address, streams, maxStreams)` — 解析済みのAudio stream候補を返す
- `espUsbHostSelectAudioOutputStream(streams, count, filter)` — 候補をスコアリングして最適なものを返す
- `usb.audioOutputStart(stream, sampleRate, address)` — 選択した出力streamを開始する
- `AudioFileSourcePROGMEM(data, size)` — 埋め込みMP3データをESP8266Audioのソースとしてラップ
- `AudioGeneratorMP3` / `mp3gen->begin(src, output)` / `mp3gen->loop()` — MP3デコードを駆動。`mp3gen->loop()`を`loop()`から呼ぶ
- `RingBufferOutput::SetRate(hz)` — デコーダがMP3ソースレートを通知するために呼ぶ。リサンプラーをリセット
- `RingBufferOutput::ConsumeSample(sample)` — ソースレートから48 kHzへの線形補間リサンプラー。リングバッファへ書き込む

## コーデックライブラリの選び方

このリポジトリには、MP3をUSB Audio OUTへ再生するサンプルが2つあります。

| サンプル | 向いている用途 |
| --- | --- |
| `EspUsbHostAudioOutputMP3PCMFlow` | MP3だけを再生したい場合。MITライセンスで軽量、USB Audio向けのPCM変換も扱いやすいです。 |
| `EspUsbHostAudioOutputMP3ESP8266Audio` | ESP8266Audioとの連携を確認したい場合。MP3以外にも複数形式へ対応する多機能ライブラリですが、GPL-3.0ライセンスに注意が必要です。 |

MP3だけであれば、まずPCMFlow版を使うのがシンプルです。このESP8266Audio版は、有名なESP8266Audioを既存プロジェクトで使っている場合や、ESP8266Audioのコーデック層との連携を確認したい場合に向いています。

## シリアル出力例

```
EspUsbHost Audio Output MP3 ESP8266Audio example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: device: address=1 portId=0x01 vid=xxxx pid=xxxx class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=192 interval=1
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## アセット

同梱の効果音は [OpenGameArt.org](https://opengameart.org) にて Juhani Junkala が公開している
[512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style) から使用しています。
ライセンスは [CC0 1.0 Universal（パブリックドメイン）](https://creativecommons.org/publicdomain/zero/1.0/) です。
