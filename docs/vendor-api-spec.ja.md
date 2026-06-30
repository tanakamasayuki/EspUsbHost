# EspUsbHost Vendor Bulk/Control API 仕様案

## 目的

`EspUsbHost` に、HID ではない vendor-specific interface (`bInterfaceClass == 0xff`) を扱うための汎用 API を追加する。

対象は vendor-specific interface の bulk IN / bulk OUT と、EP0 の vendor control IN / OUT に限定する。HID vendor report、CDC/VCP、MSC、MIDI、Audio など既存クラス API とは別扱いにする。

この仕様は、ペアになる Device 側ライブラリ `/home/mt/dev/EspUsbDevice/` の `EspUsbDeviceVendor` からの要求を受けて定義する。Device 側は vendor-specific interface、bulk OUT endpoint、bulk IN endpoint、vendor control request callback、WebUSB URL 設定を実装済みで、Host 側 API 待ちの状態。

## Device 側の現在仕様

`EspUsbDeviceVendor` の現在仕様は以下。

- interface class: `0xff`
- interface subclass: `0x00`
- interface protocol: `0x00`
- endpoint count: 2
- endpoint type: bulk OUT + bulk IN
- default endpoint MPS: 64 bytes
- endpoint address: OUT `endpointNumber`、IN `0x80 | endpointNumber`
- `tests/peer/usb_vendor` の VID/PID: `303a:4019`
- `tests/peer/usb_vendor` の product: `EspUsbDevice USB Vendor`

Device 側 peer test は現時点で interface / bulk endpoint の列挙まで確認している。Host API 追加後に bulk/control の自動テストへ拡張する。

## 取り込む範囲

今回取り込む最小範囲は次の 3 点。

1. vendor-specific interface を claim する
2. bulk OUT / bulk IN で送受信する
3. EP0 vendor control IN / OUT を送る

この範囲で、`EspUsbDeviceVendor` 側が提供している以下の Host 側検証を可能にする。

- Host -> Device bulk OUT: `ping`
- Device -> Host bulk IN: `echo:ping`
- control IN `bRequest=0x01`: `EspUsbDeviceVendor`
- control OUT `bRequest=0x02`: status success

## 取り込まない範囲

次は今回の仕様には含めない。

- WebUSB BOS descriptor の解釈
- WebUSB landing URL の取得 API
- Microsoft OS 2.0 descriptor の解釈
- Host OS / browser 連携の自動テスト
- HID vendor input/output/feature report の置き換え
- vendor-specific protocol の高レベル解釈
- 複数 alternate setting の自動切り替え
- interrupt / isochronous endpoint を使う vendor-specific interface

WebUSB / Microsoft OS 2.0 descriptor は Host OS や browser 依存が強いため、`EspUsbHost` の自動テスト対象にはしない。

## 公開データ型

```cpp
struct EspUsbHostVendorInterface {
  uint8_t address;
  uint8_t interfaceNumber;
  uint8_t inEndpoint;
  uint8_t outEndpoint;
  uint16_t inMaxPacketSize;
  uint16_t outMaxPacketSize;
};

struct EspUsbHostVendorData {
  uint8_t address;
  uint8_t interfaceNumber;
  uint8_t endpoint;
  const uint8_t *data;
  size_t length;
};
```

`EspUsbHostVendorData::data` はコールバック中だけ有効なバッファとする。保持したい場合はユーザー側でコピーする。

## 公開 API

最初に入れる API は以下とする。

```cpp
using VendorDataCallback = std::function<void(const EspUsbHostVendorData &)>;

bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t interfaceNumber = 0xff);

bool vendorWrite(const uint8_t *data,
                 size_t length,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t vendorRead(uint8_t *buffer,
                  size_t length,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

void onVendorData(VendorDataCallback callback);

bool vendorControlIn(uint8_t request,
                     uint16_t value,
                     uint16_t index,
                     uint8_t *data,
                     size_t length,
                     size_t *actualLength = nullptr,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = 1000);

bool vendorControlOut(uint8_t request,
                      uint16_t value,
                      uint16_t index,
                      const uint8_t *data = nullptr,
                      size_t length = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = 1000);
```

## API 挙動

### `vendorOpen`

`bInterfaceClass == 0xff` の interface を claim し、bulk IN / bulk OUT endpoint を記録する。

- `address == ESP_USB_HOST_ANY_ADDRESS` の場合、最初に見つかった対象 device を使う
- `interfaceNumber == 0xff` の場合、最初に見つかった vendor-specific interface を使う
- `interfaceNumber != 0xff` の場合、その interface number のみを対象にする
- bulk IN と bulk OUT の両方がある interface を優先する
- 片方向 endpoint しかない interface は、初期実装では `vendorOpen()` 失敗扱いにする
- claim 済み interface に対する再呼び出しは成功扱いにする

既存の HID vendor API との混同を避けるため、HID interface (`bInterfaceClass == 0x03`) は対象外にする。

`EspUsbDeviceVendor` との peer test では interface 0 が対象になる。ただし composite device では vendor interface number が 0 とは限らないため、API 仕様としては descriptor から検出した interface number を使う。

### `vendorWrite`

`vendorOpen()` 済みの bulk OUT endpoint にデータを送信する。

- 成功時 `true`
- 未 open、対象なし、bulk OUT なし、転送投入失敗は `false`
- `data == nullptr && length > 0` は `false`
- `length == 0` はゼロ長 bulk OUT を送るかどうか実装時に確認する。ESP-IDF 側で扱いが安定しない場合は `false` として明記する

戻り値は bool のままにする。送信 byte 数が必要になった場合は、後続で `vendorWriteAvailable()` または `vendorWrite(..., actualLength)` のような追加 API を検討する。

### `vendorRead`

受信済み bulk IN データをユーザー supplied buffer にコピーして返す。

- 戻り値はコピーした byte 数
- 受信済みデータがない場合は `0`
- `buffer == nullptr` または `length == 0` は `0`
- 初期実装ではノンブロッキング API とする

受信は内部で継続 transfer を張り、到着時にリングバッファへ保存する。`onVendorData()` が登録されている場合も、`vendorRead()` 用のバッファリングは維持する。

初期リングバッファサイズは device ごとに 512 bytes とする。これは `EspUsbDeviceVendor` の default MPS 64 bytes に対して 8 packet 分で、peer の `echo:ping` テストには十分な余裕がある。バッファサイズの setter は初期 API には含めない。

### `onVendorData`

bulk IN データ受信時に callback を呼ぶ。

- callback は USB task 側の文脈で呼ばれるため、重い処理やブロッキング処理は避ける
- callback 中の `data` は callback 終了後に無効
- 複数 device 対応のため `address` と `interfaceNumber` を渡す

### `vendorControlIn` / `vendorControlOut`

EP0 control transfer で vendor request を送る。

- `vendorControlIn` は `bmRequestType = 0xc0` を使う
- `vendorControlOut` は `bmRequestType = 0x40` を使う
- `wValue` は `value`
- `wIndex` は `index`
- `actualLength` は IN 転送で実受信 byte 数を返す
- timeout 時は `false`

初期仕様では recipient は device とする。interface recipient (`0xc1` / `0x41`) を使いたい場合は、後続で request type を指定できる低レベル API を追加検討する。

`EspUsbDeviceVendor` 側の peer device は `bmRequestType` の direction bit と `bRequest` を見て応答するため、device recipient (`0xc0` / `0x40`) で検証できる。

## interface claim 方針

現状の `EspUsbHost` は descriptor parse 時に既知クラスを自動 claim している。vendor-specific interface は既存の VCP 検出 (`vendorSerialSupported`) と衝突しうるため、初期実装では自動 claim ではなく `vendorOpen()` 明示呼び出しで claim する。

ただし、`vendorOpen()` 実行時点で device handle と descriptor 情報が保持されている必要がある。実装では、現在 `parseConfigDescriptor()` で保存している `EspUsbHostInterfaceInfo` / `EspUsbHostEndpointInfo` を利用し、未 claim の vendor-specific interface を後から claim できる経路を追加する。

## 複数 device / 複数 interface

初期仕様では「単一 active vendor interface per device」を扱う。

- 複数 device は `address` で選択可能にする
- 1 device 内の複数 vendor interface は `interfaceNumber` で選択可能にする
- 同一 device 内で複数 vendor interface を同時 open する API は初期範囲外

必要になった場合、後続で `getVendorInterfaces()` や interface handle 型を追加する。

## 受け入れテスト

`tests/peer/usb_vendor` が追加されたら、以下を自動テストの完了条件にする。

- device enumeration 後、Host が vendor-specific interface と bulk IN / OUT endpoint を検出できる
- VID/PID `303a:4019` の `EspUsbDevice USB Vendor` を検出できる
- `vendorOpen()` が成功する
- Host が `vendorWrite("ping", 4)` を送る
- Device が `echo:ping` を bulk IN で返す
- Host が `onVendorData()` または `vendorRead()` で `echo:ping` を受け取る
- Host の `vendorControlIn(0x01, ...)` が `EspUsbDeviceVendor` を受け取る
- Host の `vendorControlOut(0x02, ...)` が成功する
- Device 側 Serial に `DEVICE_STATUS rx=... control=...` 相当の成功状態が出る

Device 側 `/home/mt/dev/EspUsbDevice/tests/peer/usb_vendor` の現在の期待ログは以下。

- Host: `INTERFACE number=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2`
- Host: `VENDOR_ENUM interface=1 bulk_out=1 bulk_in=1`
- Device: `DEVICE_STATUS rx=0 control=0`

Host API 追加後は、同じ peer test に bulk echo と control request の期待ログを追加する。

## README 反映方針

実装時は README の対応表に「Vendor-specific bulk/control」を追加し、HID vendor とは別項目にする。

API セクションは HID output の近くではなく、USB serial / MIDI / Audio と同列の「Vendor bulk/control」として追加する。

## 保留事項

- `vendorWrite()` の timeout / 同期完了待ちを公開するか
- `vendorRead()` の内部バッファサイズを将来 configurable にするか
- control request の recipient を device 固定にするか、interface recipient も初回から入れるか
- `getVendorInterfaces()` を初回から入れるか
- `EspUsbHostVendorInterface` を公開するだけでなく、列挙 API を追加するか
