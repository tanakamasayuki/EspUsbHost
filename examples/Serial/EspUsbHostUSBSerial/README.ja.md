# EspUsbHostUSBSerial

> English: [README.md](README.md)

USBシリアルデバイスとESP32のUART間でデータを双方向に中継するサンプルです。

対応デバイスの種類はVIDから自動的に判別されます：

| 種類 | 例 |
|------|----|
| CDC ACM | マイコン開発ボード（Arduino・ESP32など）、モデム |
| FTDI（VCP） | FT232R・FT231Xなど FTDIチップ搭載デバイス |
| CP210x（VCP） | Silicon Labs CP2102・CP2104など |
| CH34x（VCP） | CH340・CH341など |

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBシリアルデバイス（上記の対応種類のいずれか）

## 動作内容

- USBシリアルデバイスから受信したデータを`Serial`（UART）へ転送
- シリアルモニターで入力したデータをUSBシリアルデバイスへ転送
- Arduinoの`Stream`/`Print`互換ラッパーである`EspUsbHostCdcSerial`を使用

> **注意:** スケッチには5秒の起動ディレイ（`delay(5000)`）があります。これはシリアルモニターを接続する時間を確保するためです。

## 主要API

- `EspUsbHostCdcSerial CdcSerial(usb)` — `EspUsbHost`インスタンスに紐づいたシリアルストリームを生成
- `CdcSerial.begin(baud)` — 指定ボーレートでシリアルポートを初期化
- `CdcSerial.setRxBufferSize(bytes)` — 受信リングのサイズを変更（スケッチにはコメントアウトで用意。下記参照）
- `CdcSerial.available()` / `CdcSerial.read()` — USBデバイスからデータを受信
- `CdcSerial.write(data)` — USBデバイスへデータを送信
- `usb.onDeviceConnected(callback)` — デバイス接続時に通知

## 受信バッファサイズ

受信したバイトは、USB client taskが書き込み`read()`が読み出すリングバッファに蓄えられます。既定は512バイトで、溢れると最も古いバイトをエラーも返さずに捨てます。症状として現れるのは戻り値の失敗ではなく、データの欠落や文字化けです。

512バイトは921600 baudで約5.5ms分、115200 baudでも約44ms分しかありません。そのため`loop()`が時間内に読み出せない場合（WiFiのブロッキング呼び出し、SDへの書き込み、画面描画など）や、デバイスがバースト送信する場合（1秒分のNMEAをまとめて出すGPS、起動時のログダンプなど）に不足します。取りこぼす場合はリングを拡張してください。スケッチにはコメントアウトした状態で用意してあります：

```cpp
// CdcSerial.setRxBufferSize(8192);
CdcSerial.begin(115200);
```

バッファはヒープから確保するため、`begin()`より前（または`end()`の後）に呼ぶ必要があります。attach中はUSB client taskがリングへ書き込んでいるためです。attach中・サイズが2未満・確保失敗のいずれかでは`false`を返します。現在のサイズは`CdcSerial.rxBufferSize()`で取得できます。

コンパイル時の既定値を変更する方法もありますが非推奨です。[メインREADME](../../../README.ja.md)の「USBシリアル（CDC ACM・VCP）」の節を参照してください。

## シリアル出力例

```
EspUsbHost USB serial example start
connected: device: address=1 portId=0x01 vid=0403 pid=6001 class=0x00(Device) speed=full product="FT232R USB UART"
```

接続後は、USBシリアルデバイスから送られたデータがシリアルモニターに表示され、シリアルモニターで入力したテキストがUSBデバイスへ転送されます。
