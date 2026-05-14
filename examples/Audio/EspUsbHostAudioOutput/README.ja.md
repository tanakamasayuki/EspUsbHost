# EspUsbHostAudioOutput

USBスピーカーなどのUSB Audio isochronous OUTエンドポイントへ生ペイロードを送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 注意

- このサンプルは48 kHz、16-bit、mono PCMチャンクを送信します。
- 検出したUSB Audioストリームのフォーマット情報を表示し、対応しているOUTストリームが見つかった場合だけ送信を開始します。
- 使用するデバイスに合わせてスケッチ内の`SAMPLE_RATE`とサンプル形式の定数を調整してください。
- デバイスによってはFeature Unitやクロック制御の追加対応が必要になる場合があります。

## シリアル出力例

```
EspUsbHost Audio Output example start
connected: addr=1 portId=0x01 vid=1234 pid=5678 product=USB Speaker
audio stream: iface=1 alt=1 ep=0x01 dir=OUT channels=1 bytes=2 bits=16 rate=48000 rates=1 max=98 interval=1
audio output ready: addr=1
```
