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
