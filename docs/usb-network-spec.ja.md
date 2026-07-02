# EspUsbHost USB Network API 仕様案

## 目的

`EspUsbHost` に、USB Ethernet adapter を扱うための汎用 API を追加する。

対象は特定 VID/PID の専用ドライバではなく、USB 標準クラスの CDC-ECM と CDC-NCM を中心にする。ASIX AX88179A、Realtek RTL815x 系、その他 USB NIC で見られる vendor-specific / CDC-NCM / CDC-ECM / Mass Storage などの複数 configuration 構成でも、対応する configuration だけを選択して開ける設計にする。

最終的には lwIP に `netif` として統合し、DHCP client/server、static IP、gateway、DNS、NAT などの設定を扱えるようにする。

## 現在の調査結果

`tests/manual/usb_network_descriptor` により、実機 USB NIC の configuration を横断して CDC-ECM / CDC-NCM 候補を検出できることを確認した。

AX88179A (`VID=0b95 PID=1790`) の実測結果:

```text
active config=1: Vendor Specific
config=2: CDC-NCM complete=yes
config=3: CDC-ECM complete=yes
```

active configuration は vendor-specific だったが、ESP-IDF の `usb_host_get_config_desc()` により active 以外の configuration descriptor を取得できた。これにより、device 固有 VID/PID に依存せず、CDC-NCM / CDC-ECM configuration を探索できる。

## 非目的

初期実装では以下を扱わない。

- ASIX / Realtek などの vendor-specific Ethernet protocol
- checksum offload、TSO、VLAN offload、Jumbo Frame の最適化
- 高スループット最適化
- USB 3.x SuperSpeed 固有機能
- 複数 USB NIC の同時 lwIP 統合
- bridge 機能
- PPP / RNDIS / MBIM

vendor-specific protocol は、標準 CDC-ECM / CDC-NCM で不十分な実機が見つかった場合に個別検討する。

## 対応プロトコルの優先順位

configuration 候補は以下の順で選ぶ。

1. CDC-NCM
2. CDC-ECM
3. vendor-specific Ethernet protocol

CDC-NCM を優先する理由は、近年の USB Ethernet adapter では NCM が本命の標準プロトコルになっており、ECM より転送効率が高いから。スループットを主目的にはしないが、NCM 対応 device では NCM を選ぶ方が OS driver の実績とも合いやすい。

CDC-ECM は、NCM 非対応 device や簡易実装の fallback とする。

## configuration 選択方針

USB NIC では、1つの device が複数 configuration を持つことがある。

例:

- vendor-specific driver 用 configuration
- CDC-NCM configuration
- CDC-ECM configuration
- driver 配布用 Mass Storage configuration

`EspUsbHost` は全 configuration descriptor を走査し、対応可能な network candidate を選ぶ。対応していない configuration や Mass Storage configuration は開かない。

公開候補情報は `EspUsbHostNetworkInterfaceInfo` で表す。

```cpp
enum EspUsbHostNetworkProtocol : uint8_t {
  ESP_USB_HOST_NETWORK_PROTOCOL_NONE = 0,
  ESP_USB_HOST_NETWORK_PROTOCOL_CDC_ECM,
  ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM,
};

struct EspUsbHostNetworkInterfaceInfo {
  uint8_t address;
  uint8_t configurationValue;
  EspUsbHostNetworkProtocol protocol;
  uint8_t controlInterfaceNumber;
  uint8_t controlInterfaceAlternate;
  uint8_t dataInterfaceNumber;
  uint8_t dataInterfaceAlternate;
  uint8_t macAddressStringIndex;
  uint16_t maxSegmentSize;
  uint8_t notificationEndpoint;
  uint16_t notificationMaxPacketSize;
  uint8_t inEndpoint;
  uint8_t outEndpoint;
  uint16_t inMaxPacketSize;
  uint16_t outMaxPacketSize;
};
```

`complete()` が true の候補だけを open 対象にする。

## 公開 API 案

初期 API は「descriptor 調査」「network device open」「raw Ethernet frame RX/TX」「lwIP attach」を段階的に分ける。

### descriptor 調査

実装済み:

```cpp
size_t getNetworkInterfaces(uint8_t address,
                            EspUsbHostNetworkInterfaceInfo *interfaces,
                            size_t maxInterfaces);
```

この API は全 configuration descriptor を取得するため、USB client task の callback からは呼ばない。`onDeviceConnected()` では address だけ記録し、`loop()` 側など別文脈で呼ぶ。

### network open

追加予定:

```cpp
bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
bool networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`networkOpen(address)` は対象 device の candidate を取得し、CDC-NCM、CDC-ECM の順で最初の complete candidate を選ぶ。

`networkOpen(info)` は明示的に選んだ configuration / interface を開く。

open 時の処理:

1. 対象configurationがactiveになるようにenumeration時選択または再enumerationする
2. control interface と data interface を claim
3. data interface の alternate setting を有効化
4. notification interrupt IN を必要なら開始
5. bulk IN transfer を開始
6. protocol ごとの初期化 control request を実行

既存の CDC ACM serial とは分離する。CDC control class でも subclass が ECM/NCM のものは ACM として扱わない。

### raw Ethernet frame API

lwIP 統合前の検証用に、raw frame API を用意する。

```cpp
struct EspUsbHostNetworkFrame {
  uint8_t address;
  EspUsbHostNetworkProtocol protocol;
  const uint8_t *data;
  size_t length;
};

using NetworkFrameCallback = std::function<void(const EspUsbHostNetworkFrame &)>;

void onNetworkFrame(NetworkFrameCallback callback);
bool networkWriteFrame(const uint8_t *frame,
                       size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t networkReadFrame(uint8_t *buffer,
                        size_t length,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`onNetworkFrame()` の callback は USB task 側で呼ばれる可能性があるため、重い処理やブロッキング処理は禁止する。lwIP 統合時は callback ではなく、内部 mailbox / pbuf 経由で tcpip thread へ渡す。

## CDC-ECM 方針

CDC-ECM は Ethernet frame を bulk endpoint にそのまま流す。

受信:

- bulk IN packet の payload を Ethernet frame として扱う
- short packet または transfer 完了単位で frame 境界を判断する
- MTU は Ethernet descriptor の `wMaxSegmentSize` が取れる場合はそれを優先し、取れない場合は 1514 bytes を既定値にする

送信:

- Ethernet frame を bulk OUT に送る
- device が要求する場合は ZLP の要否を検討する
- 初期実装では1 frame 1 transferとする

control / notification:

- interrupt IN endpoint は link status notification を受けるために使う
- 初期実装では link up/down のログと状態更新まで

## CDC-NCM 方針

CDC-NCM は Ethernet frame を NTB (Network Transfer Block) に包んで bulk endpoint へ流す。

初期実装は単純化する。

- NTB format: 16-bit NCM Transfer Header / Datagram Pointer Table
- 1 NTB に 1 Ethernet frame から開始
- 複数 frame aggregation は後回し
- NCM parameters を control request で取得し、最大 NTB サイズを device に合わせる
- 初期値は小さめにし、ESP32-S3 のメモリと full-speed/high-speed差を優先する

受信:

1. bulk IN で NTB を受信
2. NTH / NDP を検証
3. datagram offset / length から Ethernet frame を取り出す
4. raw frame callback または lwIP 入力へ渡す

送信:

1. Ethernet frame を含む NTB を組み立てる
2. bulk OUT に送る
3. 必要に応じて ZLP を扱う

NCM の高効率化は後回しにする。まずは DHCP が通る程度の正しさを優先する。

## lwIP 統合方針

最終形では USB NIC を lwIP の `netif` として登録する。

想定 API:

```cpp
struct EspUsbHostNetworkConfig {
  bool dhcpClient = true;
  bool dhcpServer = false;
  bool nat = false;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;
};

bool networkAttachNetif(const EspUsbHostNetworkConfig &config,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkDetachNetif(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkSetConfig(const EspUsbHostNetworkConfig &config,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

設計上は、USB class driver 層と lwIP netif 層を分ける。

- USB class driver 層: ECM/NCM、endpoint、control request、frame RX/TX
- network adapter 層: Ethernet frame queue、MAC address、link state
- lwIP integration 層: `netif`, DHCP, DNS, NAT, gateway 設定

この分離により、lwIP に繋がず raw frame だけでテストできる。

## NAT / gateway 方針

NAT は lwIP の NAPT 機能に依存する。Arduino-ESP32 の core / IDF 設定によって有効化可否が変わるため、API は段階的にする。

想定用途:

- Wi-Fi STA でインターネット接続し、USB NIC 側を LAN とする
- USB NIC 側を WAN として、Wi-Fi AP 側へ NAT する
- USB NIC を static gateway として使う

初期方針:

- NAT が有効な build では `networkSetNat(true)` 相当を提供
- NAT 非対応 build では API は `false` を返し、`lastError()` に理由を残す
- default route / gateway は lwIP netif の優先度と route 設定で扱う
- DHCP server は USB NIC を LAN 側にする用途で後続追加する

NAT は USB class driver の責務ではなく lwIP integration 層の責務にする。

## device lifecycle

接続時:

1. device descriptor と active configuration を保存
2. 全 configuration から network candidate を検出可能にする
3. 自動 open はしない
4. ユーザーが `networkOpen()` または lwIP attach API を呼ぶ

切断時:

1. bulk / interrupt transfer を停止
2. interface を release
3. lwIP netif を down にする
4. DHCP / NAT 状態を解除
5. callback には disconnect を通知

hotplug 後の再接続では address が変わる可能性があるため、VID/PID ではなく device address と内部 state を再構築する。

## HCD channel / resource 方針

ESP32-S3 は USB host channel が少ない。USB NIC は最低でも EP0、bulk IN、bulk OUT、interrupt IN を使う可能性があり、Hub 経由ではさらに厳しい。

方針:

- NIC は明示 open まで interface claim しない
- CDC-NCM/ECM候補検出だけでは endpoint channel を消費しない
- `networkOpen()` 前に `estimatedHcdChannelCount()` を使った警告を出せるようにする
- S3 では単一 NIC 直結を基本にする
- P4 では HS/FS port 差を manual test で確認する

## テスト方針

### manual

実装済み:

- `tests/manual/usb_network_descriptor`
  - 全 configuration から CDC-ECM / CDC-NCM candidate を検出
  - AX88179A で config 2 NCM / config 3 ECM を確認済み

追加予定:

- `usb_network_open`
  - candidate selection
  - `SET_CONFIGURATION`
  - interface claim
  - endpoint start
  - 手動 `SET_CONFIGURATION` 後のclaimは、ESP-IDF Host側のactive descriptor cache制約によりAX88179Aで失敗した
  - enumeration時configuration選択または再enumeration設計が必要
- `usb_network_ecm_frame`
  - raw Ethernet frame RX/TX
  - DHCP discover 送信または固定frame loopback確認
- `usb_network_ncm_frame`
  - NTB parse/build
  - raw Ethernet frame RX/TX
- `usb_network_lwip`
  - DHCP client
  - static IP
  - ping
- `usb_network_nat`
  - NAT 有効 build で USB NIC と Wi-Fi 間の forwarding を確認

### peer

CDC-ECM/NCM の peer device を Arduino Core 標準機能だけで用意するのは難しい。自動 peer test は、兄弟 device library で ECM/NCM device を実装できる場合に検討する。

当面は manual test を主軸にする。

## 実装段階

### Phase 0: descriptor 調査

完了済み。

- `EspUsbHostNetworkInterfaceInfo`
- `getNetworkInterfaces()`
- `usb_network_descriptor` manual test
- CDC ACM と ECM/NCM の自動claim分離

### Phase 1: configuration selection / open

次に実装する。

- `networkOpen()`
- `networkClose()`
- `SET_CONFIGURATION`
- interface claim / release
- data alternate setting
- notification endpoint
- endpoint channel 使用量のログ

この段階では Ethernet frame はまだ lwIP に渡さない。

注意:
ESP-IDF の `usb_host_get_active_config_descriptor()` はenumeration時にcacheされたactive configurationを返す。実機AX88179Aで手動 `SET_CONFIGURATION` 後にinterface claimを試したところ、Host側のcached active descriptorが更新されず、config 2 の data interface claim が `ESP_ERR_NOT_FOUND` で失敗した。

ESP-IDFには `usb_host_config_t::enum_filter_cb` があり、enumeration開始時にconfiguration番号を選べる。ただしcallbackに渡されるのはdevice descriptorのみで、configuration descriptorは読めない。そのため、完全な汎用自動選択には以下のどちらかが必要。

- いったんdefault configurationでenumerationし、全configurationを調査した後、選択結果を保存してdeviceを再enumerationする
- ユーザーが事前にVID/PIDやconfiguration値を指定し、enum filterでそのconfigurationを選ぶ

AX88179A専用のVID/PID分岐にはしない。汎用実装としては、調査結果を保存して再enumerationする方式を優先検討する。

### Phase 2: CDC-ECM raw frame

- ECM frame RX/TX
- raw frame callback / read API
- link status notification

ECM は NCM より実装が単純なので、raw frame API の検証に使う。

### Phase 3: CDC-NCM raw frame

- NCM parameter 取得
- NTH/NDP parse
- 1 NTB 1 frame の送受信
- aggregation は後回し

AX88179A の本命経路は NCM として扱う。

### Phase 4: lwIP netif

- Ethernet netif 登録
- pbuf RX/TX
- DHCP client
- static IP
- link up/down

### Phase 5: routing / NAT / DHCP server

- default route / gateway 設定
- DNS 設定
- NAPT 有効化
- DHCP server
- Wi-Fi STA/AP との組み合わせ例

## 受け入れ条件

最小受け入れ条件:

- AX88179A 以外の CDC-ECM または CDC-NCM USB NIC でも descriptor candidate を検出できる
- vendor-specific configuration だけを誤って開かない
- CDC ACM serial 既存機能が壊れない
- CDC-ECM で raw Ethernet frame を送受信できる
- CDC-NCM で raw Ethernet frame を送受信できる
- lwIP netif として DHCP client で IP を取得できる
- static IP / gateway / DNS を設定できる
- NAT 非対応 build で明確に失敗を返せる
- NAT 対応 build で USB NIC と Wi-Fi 間の forwarding ができる

## 未決事項

- `SET_CONFIGURATION` を既存 device state に対して行った後、descriptor state をどう再構築するか
- active configuration 変更時に ESP-IDF 側の cached active descriptor が更新されるか
- ECM/NCM の MAC address string をどの timing で読むか
- NCM transfer buffer size の既定値
- ZLP が必要な device の扱い
- lwIP netif の名前と複数 NIC 対応時の命名
- Arduino-ESP32 の NAT/NAPT 有効化条件
- Wi-Fi と USB NIC の default route 優先順位 API
