# EspUsbHostConsumerControl

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
| `0x00e2` | ミュート |
| `0x00e9` | 音量アップ |
| `0x00ea` | 音量ダウン |
| `0x00cd` | 再生/一時停止 |
| `0x00b5` | 次のトラック |
| `0x00b6` | 前のトラック |

## 主要API

- `usb.onConsumerControl(callback)` — 押下・解放時に`EspUsbHostConsumerControlEvent`付きで呼ばれる
  - `event.pressed` — 押下でtrue、解放でfalse
  - `event.usage` — HIDユーセージコード（16ビット）

## シリアル出力例

```
connected: vid=045e pid=07a5 product=USB Keyboard
consumer press usage=0x00e9 Volume Up
consumer release usage=0x00e9 Volume Up
consumer press usage=0x00cd Play/Pause
consumer release usage=0x00cd Play/Pause
```
