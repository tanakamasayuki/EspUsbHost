# EspUsbHostKeyboard

> English: [README.md](README.md)

USBキーボードからHID入力を受け取り、入力した文字をシリアルモニターに表示するとともに、CapsLock・NumLock・ScrollLock LEDをキー操作に連動して制御するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDキーボード

## 動作内容

- 日本語キーボードレイアウト（`ESP_USB_HOST_KEYBOARD_LAYOUT_JP`）を設定
- 接続・切断時にデバイスのVID・PID・製品名を表示
- キーボードで入力した印字可能なASCII文字をシリアル出力する（Enterで改行）
- CapsLock・NumLock・ScrollLockキーの押下ごとに状態をトグルしてキーボードのLEDを更新
- キーボード接続時に現在のLED状態を再適用

## 主要API

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JP)`
- `usb.onKeyboard(callback)` — キー押下・解放時に`EspUsbHostKeyboardEvent`付きで呼ばれる
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — LED状態をキーボードに送信
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
connected: vid=045e pid=07a5 product=USB Keyboard
hello world
disconnected
```

## 関連サンプル

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — キーコード・モディファイア・ASCIIを全イベントでダンプ
