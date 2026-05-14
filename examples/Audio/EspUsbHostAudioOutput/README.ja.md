# EspUsbHostAudioOutput

USBスピーカーなどのUSB Audio isochronous OUTエンドポイントへ生ペイロードを送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 注意

- このサンプルは48 kHz、16-bit、mono PCMチャンクを送信します。
- 現時点ではフォーマットディスクリプタを解釈しません。使用するデバイスに合わせてスケッチ内のサンプル形式を調整してください。
- デバイスによってはFeature Unitやクロック制御の追加対応が必要になる場合があります。

## シリアル出力例

```
EspUsbHost Audio Output example start
connected: addr=1 portId=0x01 vid=1234 pid=5678 product=USB Speaker
audio output ready: addr=1
```
