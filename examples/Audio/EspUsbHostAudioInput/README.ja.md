# EspUsbHostAudioInput

USBマイクなどのUSB Audio isochronous INデータを受信し、1秒ごとの受信バイト数を表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBマイクなどのUSB Audio入力デバイス

## 注意

- このサンプルは検出したUSB Audioストリームのフォーマット情報と、生のオーディオペイロードの受信量を表示します。
- 受信データのデコード、再生、保存は行いません。
- サンプルレート設定やFeature Unit制御が必要なデバイスは、追加対応が必要になる場合があります。

## シリアル出力例

```
EspUsbHost Audio Input example start
connected: addr=1 portId=0x01 vid=1234 pid=5678 product=USB Microphone
audio stream: iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max=96 interval=1
audio: addr=1 iface=1 bytes_per_sec=96000 last_chunk=96
```
