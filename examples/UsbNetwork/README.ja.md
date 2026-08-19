# EspUsbHost UsbNetwork

> English: [README.md](README.md)

> ⚠️ **実験的機能です。** Arduino-ESP32 3.3.11以降では、列挙時にactive
> configurationを選択できます。この例はAX88179A (`0b95:1790`) のCDC-NCM
> configuration 2を選びます。他のアダプタでは、そのまま接続すれば本sketchが接続時に
> 候補一覧とselectorへ追加する1行を表示するので、それを貼ってリセットしてください
> （→「2パス必要な理由」）。

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
  DHCP クライアントの lwIP netif として attach します
- 接続時に、**すべての** configuration から見つかった CDC-ECM / CDC-NCM 候補と現在
  active な configuration 値を表示します。未知のアダプタでも、selectorに書く値が
  シリアルログからそのまま分かります
- 取得した IP アドレスを表示します
- 任意の `HTTP_TEST_URL` を設定した場合、`HTTPClient`でUSB経由のGETを実行します

## 主な API

- `usb.networkAttachNetif(cfg, address)`: network interface を（必要なら）open し、
  `esp_netif` の netif として登録します。`EspUsbHostNetworkConfig` は既定で DHCP クライアントです。
  固定アドレスにするなら `dhcpClient=false` にして `ip`/`gateway`/`subnet`（`/dns1`）を設定します。
- `usb.setConfigurationSelector(callback)`: `usb.begin()`より前に登録し、device descriptorに
  対して選択するconfiguration値を返します。`0`はdevice既定値を維持します。callbackはUSB Host
  taskで実行されるため、ブロックしてはいけません。
- `usb.networkLocalIP(address)`: リース取得後の IP を返します。
- `usb.networkDetachNetif(address)`: netif を解除します（USB 切断時にも自動で行われます）。
- IP スタックを使わず生の Ethernet フレームを扱う場合は `usb.onNetworkFrame()` /
  `usb.networkWriteFrame()` / `usb.networkReadFrame()` を使い、netif は attach しません。

## 2パス必要な理由

CDC-NCM/ECM 機能が既定 configuration に**無い**アダプタは、1回の列挙では接続できません。

1. `setConfigurationSelector()` はUSB Host Libraryの `enum_filter_cb` で駆動され、
   渡ってくるのは **device descriptorだけ**です。この時点ではdevice handleが存在しないので、
   CDC-NCM/ECM interfaceが見えるconfiguration descriptorをselector内から読むことは
   できません。selectorが値を返す時点で番号は既知でなければなりません。
2. configuration descriptorを読むには列挙済みのdeviceが必要です。`usb.getNetworkInterfaces()`
   が `usb_host_get_config_desc()` で config `1..bNumConfigurations` を取得し、候補ごとの
   `configurationValue` を返します（本sketchが接続時に表示しているもの）。ただしその時点で
   deviceは既に別のconfigurationで動作しています。
3. `networkOpen()` / `networkAttachNetif()` は `configurationValue` が **active な**
   configuration と一致する候補しか受け付けません（active でない configuration の
   interface は claim できません）。よって判明した値が効くのは**次回の列挙**からです。

つまり、パス1で既定configurationで列挙して値を判明させ、パス2でselectorがその値を返して
再列挙する、という2段構えになります。本sketchでは、表示されたselectorルールを追加してから
ボードをリセットする操作がパス2に相当します。

### これはESP-IDF APIの制約で、USB仕様の制約ではない

TinyUSBは無関係（TinyUSBはdevice側。ここでのhost側はESP-IDFのUSB Host Library）で、
USB仕様も2パスを要求していません。`GET_DESCRIPTOR(CONFIGURATION, index)` は標準リクエストで、
deviceはAddress stateでも**全index**について応答する義務があるため、activeでない
configurationのdescriptorも普通に読めます。実際、config 1がactiveな状態でも
`getNetworkInterfaces()` が config 2 のCDC-NCMを報告できるのはこのためです。USB仕様が
定めているのは「activeなconfigurationは同時に1つ」「interfaceはactive configuration内の
ものしかclaimできない」という点だけです。

2パスになるのはESP-IDF側のAPIの都合です（Arduino-ESP32 core同梱のIDFヘッダで確認）:

- `enum_filter_cb` は `bool (*)(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue)`
  （`usb/usb_types_stack.h`）で、ドキュメントに non-blocking であることと
  **USB transferを投げてはならない**ことが明記されています。つまりselector内で
  configuration descriptorを取りに行けません。
- `usb_host_get_config_desc()`（`usb/usb_host.h`）は client handle と device handle が必要、
  すなわち列挙完了後＝configurationが確定した後にしか使えません。
- `usb_host.h` には列挙済みdeviceのconfigurationを変える公開APIがありません
  （`set_configuration` も再列挙もありません）。EP0へ自分でSET_CONFIGURATIONを投げると、
  stackが把握しているclaim済みinterfaceやpipeの状態と不整合になります。

filter callbackにconfiguration descriptorも渡されるようになれば1パスで済みます。それまでは、
値を判明させる列挙と、その値を使う列挙は別々になります。

## パス2を自動化する場合

USB host stack を再起動すれば全deviceが再列挙されるので、パス2をsketchから起こせます。
`end()` はUSB Host task上では実行を拒否するので `loop()` から、かつ以下の順序で行います。

```cpp
static uint16_t forcedVid = 0;
static uint16_t forcedPid = 0;
static uint8_t forcedConfiguration = 0;   // パス2でselectorが参照する

// setup(): begin()より前に1回だけ登録する。end()はselectorを保持するので
// 再起動時の再登録は不要。
usb.setConfigurationSelector([](const usb_device_desc_t &device) -> uint8_t {
  if (forcedConfiguration && device.idVendor == forcedVid && device.idProduct == forcedPid) {
    return forcedConfiguration;
  }
  return 0;
});

// loop(): 候補スキャンで、別configurationに complete な候補が見つかったとき
// （値がまだ確定していない場合だけ。誤判定で無限に再起動しないため）
if (!forcedConfiguration && candidate.configurationValue != nicConfiguration) {
  forcedVid = nicVid;
  forcedPid = nicPid;
  forcedConfiguration = candidate.configurationValue;

  usb.networkDetachNetif(nicAddress);  // end()の前に必須。netifは if_key "USB_NCM" 固定で、
                                       // end()経路はnetifを破棄しないため、次回のattachで
                                       // esp_netif_new()が失敗する
  usb.end();                           // 最大3秒程度。transferをdrainしdeviceをcloseする
  nicAddress = 0;                      // end()は onDeviceDisconnected() を呼ばないので、
  attached = false;                    // sketch側の状態は自分で戻す
  candidatesReported = false;
  usb.begin();                         // （begin(cfg)を使っていたなら同じcfgで）
}
```

この方式の注意点:

- 再起動サイクルのコストは概ね1〜2秒＋再列挙です。未知のアダプタの初回接続時に1回だけ発生します。
- `vid/pid → configurationValue` を `Preferences`（NVS）に保存すれば、次回起動以降は
  パス1で正しいconfigurationを選べるので再起動は消えます。
- 再起動前に `forcedConfiguration` をラッチし、同じdeviceで2回以上再起動しないでください。
  候補がactiveにならないアダプタや候補ゼロの場合、再起動ループになります。
- 再起動は接続中の**全**deviceを再列挙します。アダプタがper-port power switching対応の
  外付けハブ配下にあるなら、`usb.setHubPortPower(hubAddress, port, false/true)` で
  そのポートだけ再列挙する方法もあります。

## 注意

- configuration選択にはArduino-ESP32 3.3.11以降が必要です。
- selectorに渡されるのはdevice descriptorだけなので、selector内でconfiguration番号を
  調べることはできません。接続時に表示される候補一覧（全configurationを走査する
  `usb.getNetworkInterfaces()`による）でselectorに書く値が分かるので、ルールを追加したら
  ボードをリセットしてそのconfigurationで再列挙させてください。
- device が両方に対応している場合は CDC-NCM を CDC-ECM より優先します。
- interface の open は必ず `loop()` 文脈で行い、USB device コールバック内では行わないでください
  （enumeration descriptor へのアクセスは client task 上では不可）。
- lwIP 統合にはビルドに `esp_netif` が必要です（標準 Arduino-ESP32 core には含まれます）。
  無い場合 `networkAttachNetif()` は `false` を返し、生フレーム API は引き続き使えます。

## 関連

- [EspUsbDevice UsbNetwork](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/UsbNetwork) - 対になる USB ネットワークデバイス
- `tests/peer/usb_ncm` - 2 枚のボードによる CDC-NCM 自動 peer テスト
- `docs/usb-network-spec.ja.md` - USB network API の設計メモ
