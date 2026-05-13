# EspUsbHostUSBSerial

> English: [README.md](README.md)

USB CDC ACMシリアルデバイスとESP32のUART間でデータを双方向に中継するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB CDC ACMデバイス（USB-シリアル変換アダプタ、マイコン開発ボードなど）

## 動作内容

- USBシリアルデバイスから受信したデータを`Serial`（UART）へ転送
- シリアルモニターで入力したデータをUSBシリアルデバイスへ転送
- Arduinoの`Stream`/`Print`互換ラッパーである`EspUsbHostCdcSerial`を使用

> **注意:** スケッチには5秒の起動ディレイ（`delay(5000)`）があります。これはシリアルモニターを接続する時間を確保するためです。

## 主要API

- `EspUsbHostCdcSerial CdcSerial(usb)` — `EspUsbHost`インスタンスに紐づいたCDCシリアルストリームを生成
- `CdcSerial.begin(baud)` — 指定ボーレートでCDCシリアルポートを初期化
- `CdcSerial.available()` / `CdcSerial.read()` — USBデバイスからデータを受信
- `CdcSerial.write(data)` — USBデバイスへデータを送信
- `usb.onDeviceConnected(callback)` — デバイス接続時に通知

## シリアル出力例

```
EspUsbHost USB serial example start
connected: vid=0403 pid=6001 product=FT232R USB UART
```

接続後は、USBシリアルデバイスから送られたデータがシリアルモニターに表示され、シリアルモニターで入力したテキストがUSBデバイスへ転送されます。
