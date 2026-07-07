# EspUsbHost UsbNetwork

> English: [README.md](README.md)

USB Ethernet アダプタ（CDC-NCM / CDC-ECM）に対する USB *ホスト* として動作し、
lwIP のネットワークインターフェースとして立ち上げます。USB NIC、または兄弟ライブラリの
[EspUsbDevice `UsbNetwork`](../../../EspUsbDevice/examples/UsbNetwork/) スケッチを動かした
2 枚目のボードを挿すと、Wi-Fi なしで標準の Arduino ネットワーク（`NetworkClient` /
`HTTPClient`）が USB 経由で動きます。

これは EspUsbDevice `UsbNetwork` 例の対になるものです。あちらはネットワーク *デバイス*
（自前の DHCP サーバを `192.168.7.1` で持つ）で、こちらは `192.168.7.x` のリースを受け取って
それに到達するネットワーク *ホスト* です。

## ハードウェア

- ホスト用の ESP32-S3（または USB ホスト対応の Arduino-ESP32 ボード）
- USB ネットワークアダプタ、または EspUsbDevice `UsbNetwork` を動かす 2 枚目のボード
- ログ用の別 Serial モニタ接続

## 動作

- USB デバイスを列挙し、CDC-NCM/ECM interface があれば `networkAttachNetif()` で
  DHCP クライアントの lwIP netif として attach する
- 取得した IP アドレスを表示する
- `HTTPClient` で `http://192.168.7.1/` に GET し、USB 経由で TCP/IP が通ることを示す

## 主な API

- `usb.networkAttachNetif(cfg, address)`: network interface を（必要なら）open し、
  `esp_netif` の netif として登録する。`EspUsbHostNetworkConfig` は既定で DHCP クライアント。
  固定アドレスにするなら `dhcpClient=false` にして `ip`/`gateway`/`subnet`（`/dns1`）を設定する。
- `usb.networkLocalIP(address)`: リース取得後の IP を返す。
- `usb.networkDetachNetif(address)`: netif を解除する（USB 切断時にも自動で行われる）。
- IP スタックを使わず生の Ethernet フレームを扱う場合は `usb.onNetworkFrame()` /
  `usb.networkWriteFrame()` / `usb.networkReadFrame()` を使い、netif は attach しない。

## 注意

- device が両方に対応している場合は CDC-NCM を CDC-ECM より優先する。
- interface の open は必ず `loop()` 文脈で行い、USB device コールバック内では行わない
  （enumeration descriptor へのアクセスは client task 上では不可）。
- lwIP 統合にはビルドに `esp_netif` が必要（標準 Arduino-ESP32 core には含まれる）。
  無い場合 `networkAttachNetif()` は `false` を返し、生フレーム API は引き続き使える。

## 関連

- [EspUsbDevice UsbNetwork](../../../EspUsbDevice/examples/UsbNetwork/) - 対になる USB ネットワークデバイス
- `tests/peer/usb_ncm` - 2 枚のボードによる CDC-NCM 自動 peer テスト
- `docs/usb-network-spec.ja.md` - USB network API の設計メモ
