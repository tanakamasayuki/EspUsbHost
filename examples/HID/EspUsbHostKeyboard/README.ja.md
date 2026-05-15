# EspUsbHostKeyboard

> English: [README.md](README.md)

USBキーボードからHID入力を受け取り、入力した文字をシリアルモニターに表示するとともに、CapsLock・NumLock・ScrollLock LEDをキー操作に連動して制御するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDキーボード

## 動作内容

- 日本語キーボードレイアウト（`ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP`）を設定
- 接続・切断時にデバイスのVID・PID・製品名を表示
- キーボードで入力した印字可能なASCII文字をシリアル出力する（Enterで改行）
- CapsLock・NumLock・ScrollLockの状態はライブラリが自動管理し、LEDへの反映と`event.ascii`への適用を行う

## 主要API

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP)`
- `usb.onKeyboard(callback)` — キー押下・解放時に`EspUsbHostKeyboardEvent`付きで呼ばれる
  - `event.ascii` — CapsLock・NumLock状態を反映した文字
  - `event.capsLock` / `event.numLock` / `event.scrollLock` — このキー押下後の現在のロック状態
- `usb.getKeyboardCapsLock()` / `usb.getKeyboardNumLock()` / `usb.getKeyboardScrollLock()` — いつでもロック状態を読み出し可能
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — ロック状態とLEDを手動でオーバーライド
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
connected: vid=045e pid=07a5 product=USB Keyboard
hello world
disconnected
```

## 関連サンプル

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — キーコード・モディファイア・ASCIIを全イベントでダンプ
