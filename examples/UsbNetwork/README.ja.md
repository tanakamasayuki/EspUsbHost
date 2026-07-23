# EspUsbHost UsbNetwork

> English: [README.md](README.md)

> ⚠️ **実験的機能です。** Arduino-ESP32 3.3.11以降では、列挙時にactive
> configurationを選択できます。この例はAX88179A (`0b95:1790`) のCDC-NCM
> configuration 2を選びます。他のアダプタでは `tests/manual/usb_network_descriptor`
> でconfigurationを調査し、selectorへ規則を追加してください。

USB Ethernet アダプタ（CDC-NCM / CDC-ECM）に対する USB *ホスト* として動作し、
lwIP のネットワークインターフェースとして立ち上げます。USB NIC、または兄弟ライブラリの
[EspUsbDevice `UsbNetwork`](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/UsbNetwork) スケッチを動かした
2 枚目のボードを挿すと、Wi-Fi なしで標準の Arduino ネットワーク（`NetworkClient` /
`HTTPClient`）が USB 経由で動きます。

これは EspUsbDevice `UsbNetwork` 例の対になるものです。あちらはネットワーク *デバイス*
（自前の DHCP サーバを `192.168.7.1` で持つ）で、こちらは `192.168.7.x` のリースを受け取って
それに到達するネットワーク *ホスト* です。

## ハードウェア

- ホスト用の ESP32-S3（または USB ホスト対応の Arduino-ESP32 ボード）
- CDC-NCM/ECM対応USB Ethernetアダプタ、またはEspUsbDevice `UsbNetwork`を動かす
  2枚目のESP32-S3
- ログ用の別 Serial モニタ接続

## 動作

- USB デバイスを列挙し、CDC-NCM/ECM interface があれば `networkAttachNetif()` で
  DHCP クライアントの lwIP netif として attach する
- 取得した IP アドレスを表示する
- 任意の `HTTP_TEST_URL` を設定した場合、`HTTPClient`でUSB経由のGETを実行する

## 主な API

- `usb.networkAttachNetif(cfg, address)`: network interface を（必要なら）open し、
  `esp_netif` の netif として登録する。`EspUsbHostNetworkConfig` は既定で DHCP クライアント。
  固定アドレスにするなら `dhcpClient=false` にして `ip`/`gateway`/`subnet`（`/dns1`）を設定する。
- `usb.setConfigurationSelector(callback)`: `usb.begin()`より前に登録し、device descriptorに
  対して選択するconfiguration値を返す。`0`はdevice既定値を維持する。callbackはUSB Host
  taskで実行されるため、ブロックしてはいけない。
- `usb.networkLocalIP(address)`: リース取得後の IP を返す。
- `usb.networkDetachNetif(address)`: netif を解除する（USB 切断時にも自動で行われる）。
- IP スタックを使わず生の Ethernet フレームを扱う場合は `usb.onNetworkFrame()` /
  `usb.networkWriteFrame()` / `usb.networkReadFrame()` を使い、netif は attach しない。

## 注意

- configuration選択にはArduino-ESP32 3.3.11以降が必要。
- selectorに渡されるのはdevice descriptorだけなので、configuration番号は事前調査が必要。
- device が両方に対応している場合は CDC-NCM を CDC-ECM より優先する。
- interface の open は必ず `loop()` 文脈で行い、USB device コールバック内では行わない
  （enumeration descriptor へのアクセスは client task 上では不可）。
- lwIP 統合にはビルドに `esp_netif` が必要（標準 Arduino-ESP32 core には含まれる）。
  無い場合 `networkAttachNetif()` は `false` を返し、生フレーム API は引き続き使える。

## 関連

- [EspUsbDevice UsbNetwork](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/UsbNetwork) - 対になる USB ネットワークデバイス
- `tests/peer/usb_ncm` - 2 枚のボードによる CDC-NCM 自動 peer テスト
- `docs/usb-network-spec.ja.md` - USB network API の設計メモ
