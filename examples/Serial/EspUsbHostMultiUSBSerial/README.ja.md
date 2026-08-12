# EspUsbHostMultiUSBSerial

> English: [README.md](README.md)

2つの独立したUSBシリアルデバイスを同時に使うサンプルです。この例では分かりやすさを優先し、VIDでFTDIとCP210xを見分けて、それぞれ別の`EspUsbHostCdcSerial`へ割り当てます。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBハブ、または十分なHostポートを持つボード
- FTDI USBシリアルデバイス（VID `0x0403`）
- CP210x USBシリアルデバイス（VID `0x10c4`）

## 動作内容

- 接続されたデバイスの`vid`を見てFTDI用/CP210x用のストリームへ割り当てます。
- シリアルモニターに入力した文字は、接続中の両方のUSBシリアルデバイスへ送信されます。
- USBシリアルデバイスから受信したデータは、`FTDI:`または`CP210x:`のプレフィックス付きで表示されます。

## デバイスを特定する情報

| フィールド | 用途 |
|------------|------|
| `address` | 現在のUSBアドレス。`setAddress()`に渡して送受信対象を指定します。抜き差しで変わることがあります。 |
| `portId` | 接続位置。再接続後も「同じポート」を追いたい場合に使います。 |
| `vid` / `pid` | デバイスの種類やモデルを見分けるために使います。このサンプルでは`vid`でFTDI/CP210xを振り分けています。 |
| `manufacturer` / `product` | 表示名。ログには便利ですが、一意とは限りません。 |
| `serial` | USBシリアル番号文字列。デバイスが提供していれば個体識別に使えますが、空文字の場合があります。 |

## 重要な制限

- 実際に同時利用できる組み合わせは、Host側のリソース、USBハブの挙動、各デバイスのクラスドライバ対応に依存します。
- ESP32-S3 + USBハブの手動テストでは、`FTDI + CP210x` は動作確認済みです。
- `FTDI + CH34x` は、テスト環境ではHCDチャネル不足で失敗しました。
- 1つの複合USBデバイス内にある複数CDCインターフェースを分けて扱う例ではありません。別々のUSBシリアルデバイスを接続してください。

## 主要API

- `EspUsbHostCdcSerial FtdiSerial(usb)` / `Cp210xSerial(usb)` — 独立したストリームラッパーを作成
- `device.vid` — FTDI/CP210xの振り分けに使用
- `device.address` — `setAddress()`に渡す現在のUSBアドレス
- `CdcSerial.setAddress(address)` — ストリームラッパーを接続中の1デバイスへバインド
- `CdcSerial.setAddress(0)` — デバイスがない間はストリームラッパーを未割り当てにする
- `CdcSerial.setRxBufferSize(bytes)` — そのラッパーの受信リングのサイズを変更（スケッチにはコメントアウトで用意。下記参照）

## 受信バッファサイズ

受信リングはラッパーごとに独立していて、既定は512バイトです。USB client taskが書き込み、`read()`が読み出します。溢れると最も古いバイトを黙って捨てるため、`loop()`が中継する速度を超えて送ってくるデバイスではエラーもなくデータが失われます。このサンプルでは`loop()`が2つのポートと`Serial`をまとめて処理するので、片方の読み出しが遅れるともう片方にも影響します。

サイズはインスタンス単位なので、必要なポートの分だけ増やせます。スケッチにはコメントアウトした状態で用意してあります：

```cpp
// FtdiSerial.setRxBufferSize(8192);
FtdiSerial.begin(115200);
Cp210xSerial.begin(115200);
```

バッファはヒープから確保するため、そのラッパーの`begin()`より前（または`end()`の後）に呼ぶ必要があります。attach中はUSB client taskがリングへ書き込んでいるためです。attach中・サイズが2未満・確保失敗のいずれかでは`false`を返します。

## シリアル出力例

```
EspUsbHost multi USB serial example start
Connect one FTDI device and one CP210x device.
Serial Monitor input is sent to both devices.
connected: device: address=1 portId=0x01 vid=0403 pid=6001 class=0x00(Device) speed=full product="FT232R USB UART"
FTDI assigned: portId=0x01 address=1 serial=
connected: device: address=2 portId=0x02 vid=10c4 pid=ea60 class=0x00(Device) speed=full product="CP2102 USB to UART Bridge Controller"
CP210x assigned: portId=0x02 address=2 serial=0001
FTDI: hello from ftdi
CP210x: hello from cp210x
```
