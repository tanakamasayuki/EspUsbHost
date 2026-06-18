# EspUsbHostSystemControl

> English: [README.md](README.md)

USBキーボードからHIDシステムコントロールイベント（電源・スタンバイ・ウェイクアップ）を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- システムコントロールキー（電源ボタンなど）付きUSBキーボード

## 動作内容

- HIDシステムコントロールレポートを受信
- 押下・解放ごとにHIDユーセージコードと名称を表示

対応ユーセージコード（`EspUsbHost.h`で定義）：

| 定数 | 値 | 名称 |
|------|----|------|
| `ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF` | `0x01` | 電源オフ |
| `ESP_USB_HOST_SYSTEM_CONTROL_STANDBY` | `0x02` | スタンバイ |
| `ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST` | `0x03` | ウェイクアップ |

## 主要API

- `usb.onSystemControl(callback)` — 押下・解放時に`EspUsbHostSystemControlEvent`付きで呼ばれる
  - `event.pressed` — 押下でtrue、解放でfalse
  - `event.usage` — HIDユーセージコード（8ビット）
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたシステムコントロールレポートバイト
- `espUsbHostSystemControlUsageName(event.usage)` — 代表的なSystem Control usageの読みやすい名前を返す

## シリアル出力例

```
system press usage=0x81 Power Off
system release usage=0x81 Power Off
```
