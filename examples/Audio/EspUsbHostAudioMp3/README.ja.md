# EspUsbHostAudioMp3

埋め込みMP3ファイルをUSBスピーカーなどのUSB Audio出力デバイスで順番に再生するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 依存ライブラリ

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)（Arduino Library Managerで入手可能）— MP3デコード

## 注意

- MP3ファイルは `assets_embed.h` でCの配列として埋め込まれており、順番に再生されます。MP3以外のファイルはスキップされます。
- デコーダが `SetRate()` でMP3のサンプルレートを通知します。線形補間リサンプラーによってUSB出力レート（48 kHz）に変換されます。
- USB出力は48 kHz・16-bit ステレオPCMです。接続するUSBデバイスがこのフォーマットに対応している必要があります。
- 全ファイルを1周再生したら停止します。

## シリアル出力例

```
EspUsbHost Audio MP3 example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: addr=1 portId=0x01 vid=xxxx pid=xxxx product=USB Speaker
audio stream: iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 ...
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## アセット

同梱の効果音は [OpenGameArt.org](https://opengameart.org) にて Juhani Junkala が公開している
[512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style) から使用しています。
ライセンスは [CC0 1.0 Universal（パブリックドメイン）](https://creativecommons.org/publicdomain/zero/1.0/) です。
