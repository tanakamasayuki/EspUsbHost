# EspUsbHost KeyboardNKRO

> English: [README.md](README.md)

N-key rollover (NKRO) の USB キーボードをホストし、同時に押されているキー数を表示します。
boot キーボードは同時 6 キーまでですが、NKRO キーボードはキーの **ビットマップ** を送るため
この制限がありません。

EspUsbHost は両方のフォーマットを自動でデコードします。HID report descriptor から
レポートレイアウトを学習するため、boot(6KRO)でも NKRO でも `onKeyboardState()` は同じ
正規化済みキー状態を返します。設定は不要です。

## ハードウェア

- ホスト用の ESP32-S3(または USB ホスト対応の Arduino-ESP32 ボード)
- NKRO 対応 USB キーボード、または兄弟ライブラリの
  [EspUsbDevice `KeyboardNKRO`](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/KeyboardNKRO) スケッチを
  動かした 2 枚目の ESP32-S3
- ログ用の別 Serial モニタ接続

## 動作

- 各キーの press/release と、同時に押されているキー数を表示する
- 同時押しの最大数を記録する(6 を超えれば NKRO が効いている証拠)
- 接続時に NKRO ビットマップレポートを検出したかを表示する

## 主な API

- `usb.onKeyboardState(cb)`: 状態が変化したreportごとに`EspUsbHostKeyboardState`を1回通知。
  `isDown()`、`wasPressed()`、`wasReleased()`で、修飾キー`0xE0～0xE7`を含むすべての
  Keyboard/Keypad usageをboot / NKRO共通で確認できる。
- `usb.keyboardUsesBitmapReport(address)`: キーボードが NKRO ビットマップ(report
  protocol)を送るか、6 キーの boot レポートかを返す。診断用(デコード自体は自動)。

## シリアル出力例

7 キー（`a`～`g`）を同時に押し込み、1 キーを離した場合の例です。

```
Keyboard connected: 303a:4033 nkro-bitmap=1
press   keycode=0x04
held=1 (max=1) modifiers=0x00
press   keycode=0x05
held=2 (max=2) modifiers=0x00
press   keycode=0x06
held=3 (max=3) modifiers=0x00
press   keycode=0x07
held=4 (max=4) modifiers=0x00
press   keycode=0x08
held=5 (max=5) modifiers=0x00
press   keycode=0x09
held=6 (max=6) modifiers=0x00
press   keycode=0x0a
held=7 (max=7) modifiers=0x00
release keycode=0x04
held=6 (max=7) modifiers=0x00
```

`held` が 6 を超えていれば NKRO が有効です。

## 注意

- NKRO キーボードは既定で report protocol になり、ビットマップを送ります。ホストは boot
  protocol を強制しないため、NKRO が有効なままになります。
- International1-9 と LANG1-9 の usage はビットマップ範囲に含まれるので、JIS など
  非 US 配列のキーも通知されます。

## 関連

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) - 標準の boot キーボード例
- [EspUsbDevice KeyboardNKRO](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/KeyboardNKRO) - 対になる NKRO デバイス
