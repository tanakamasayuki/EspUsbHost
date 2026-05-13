# EspUsbHostKeyboardLeds

> English: [README.md](README.md)

接続されたUSBキーボードのNumLock・CapsLock・ScrollLock LEDを、HID出力レポートで制御するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- インジケーターLED付きUSB HIDキーボード

## 動作内容

シリアルモニターから1文字コマンドを送ると、対応するキーボードLEDをトグルします。

| コマンド | 動作 |
|----------|------|
| `n` | NumLockをトグル |
| `c` | CapsLockをトグル |
| `s` | ScrollLockをトグル |
| `0` | 全LEDをオフ |

## 主要API

- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — LED状態を更新するHID出力レポートを送信
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
Keyboard LED control
n: NumLock, c: CapsLock, s: ScrollLock, 0: all off
connected: vid=045e pid=07a5 product=USB Keyboard
leds: num=1 caps=0 scroll=0 OK
leds: num=1 caps=1 scroll=0 OK
```

## 関連サンプル

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — キーボード入力（キーストロークの読み取り）
