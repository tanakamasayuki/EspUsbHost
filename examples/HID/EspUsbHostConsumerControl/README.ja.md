# EspUsbHostConsumerControl

> English: [README.md](README.md)

USBキーボードやリモコンからHIDコンシューマーコントロールイベント（メディアキー）を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- コンシューマーコントロールキー（音量・再生/一時停止など）付きUSBキーボードまたはリモコン

## 動作内容

- HIDコンシューマーコントロールレポートを受信
- キーの押下・解放ごとにHIDユーセージコードと、主要なコードの名称を表示

対応している主なユーセージコード：

| ユーセージ | 名称 |
|------------|------|
| `ESP_USB_HOST_CONSUMER_CONTROL_MUTE` | ミュート |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP` | 音量アップ |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN` | 音量ダウン |
| `ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE` | 再生/一時停止 |
| `ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK` | 次のトラック |
| `ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK` | 前のトラック |

## 主要API

- `usb.onConsumerControl(callback)` — 押下・解放時に`EspUsbHostConsumerControlEvent`付きで呼ばれる
  - `event.pressed` — 押下でtrue、解放でfalse
  - `event.usage` — HIDユーセージコード（16ビット）
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたコンシューマーコントロールレポートバイト
- `espUsbHostConsumerControlUsageName(event.usage)` — 代表的なConsumer usageの読みやすい名前を返す

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
consumer press usage=0x00e9 Volume Up
consumer release usage=0x00e9 Volume Up
consumer press usage=0x00cd Play/Pause
consumer release usage=0x00cd Play/Pause
```
