# EspUsbHostKeyboardDump

> English: [README.md](README.md)

キーボードの全イベントをシリアルモニターにダンプするサンプルです。押下・解放の区別、HIDキーコード、ASCII値、アクティブなモディファイアキーを表示します。

カスタムキーボードのデバッグ、キーコードマッピングの確認、ライブラリが生成するデータを文字解釈の前段階で確認するのに使います。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDキーボード

## 動作内容

- キーの押下・解放ごとに`EspUsbHostKeyboardEvent`の全フィールドを表示
- モディファイアビットを名前付きフラグ（LSHIFT・RCTRLなど）にデコード
- レイアウト設定なし — keycode と ascii は生のHIDレポートをそのまま反映

## 主要API

- `usb.onKeyboard(callback)` — キー押下・解放時に`EspUsbHostKeyboardEvent`付きで呼ばれる

## シリアル出力例

```
connected: vid=045e pid=07a5 product=USB Keyboard
[press  ] keycode=0x04 ascii=0x61(a) modifiers=none
[release] keycode=0x04 ascii=0x61(a) modifiers=none
[press  ] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[release] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[press  ] keycode=0x39 ascii=0x00(.) modifiers=none
[release] keycode=0x39 ascii=0x00(.) modifiers=none
```

## EspUsbHostHIDRawDumpとの違い

`EspUsbHostHIDRawDump`はUSB転送の生バイトをパース前に表示します。
このサンプルはライブラリのパース結果を表示します — キーコード解決とASCII変換後のイベントです。

## 関連サンプル

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — 文字出力とLED連動
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — 生HIDレポートバイト
