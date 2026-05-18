# EspUsbHostCustomDeviceCallbacks

> English: [README.md](README.md)

標準のシリアルダンプ用ヘルパーではなく、自分でデバイス接続・切断コールバックを定義する例です。

## 動作内容

- USBデバイス接続時に独自形式の1行サマリーを表示
- 接続デバイスのインターフェースを調べ、キーボードらしいインターフェースがあるか表示
- 現在の接続デバイス数を表示
- シリアルから`d`を送ると詳細ディスクリプタ情報を全表示

## 主要API

- `usb.onDeviceConnected(onDeviceConnected)` — 名前付きの接続コールバック関数を登録
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — 名前付きの切断コールバック関数を登録
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — コールバック内でインターフェース情報を調べる
- `usb.deviceCount()` — 現在接続されているデバイス数を取得
- `usb.printAllDeviceInfo()` — 必要なときだけ詳細ディスクリプタ情報を表示
