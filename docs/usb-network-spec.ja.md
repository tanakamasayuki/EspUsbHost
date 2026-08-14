# EspUsbHost USB Network API 仕様案

> **English readers:** the design and findings for CDC-NCM / CDC-ECM USB Ethernet support,
> including why adapters that hide their network function outside the default configuration
> need `setConfigurationSelector()` and two enumeration passes. The current API is
> documented in "USB network" in [README.md](../README.md), with runnable code in
> [`examples/UsbNetwork/`](../examples/UsbNetwork/).

> ⚠️ **現状: 実験的機能。** Arduino-ESP32 3.3.11以降では
> `setConfigurationSelector()` により列挙時のconfiguration選択が可能。selectorにはdevice
> descriptorだけが渡るため、configuration番号は事前調査して指定する。AX88179Aの
> CDC-NCM configuration 2を選択する例を `examples/UsbNetwork` に実装済み。

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

一部実装済み:

```cpp
bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
bool networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`networkOpen(address)` は対象 device の candidate を取得し、CDC-NCM、CDC-ECM の順で最初の complete candidate を選ぶ。

`networkOpen(info)` は明示的に選んだ configuration / interface を開く。

現在の実装は、candidate の `configurationValue` が現在 active な configuration と一致する場合だけ open する。active configurationは`begin()`前に`setConfigurationSelector()`を登録して列挙時に選ぶ。

selectorには device descriptor しか渡らないため、CDC-NCM/ECM が既定 configuration に無い
deviceは2パスになる。パス1で列挙して `getNetworkInterfaces()`（全configurationを走査）で
`configurationValue` を判明させ、パス2でselectorがその値を返して再列挙する。自動化する場合は
`loop()`から `networkDetachNetif()`（netifは `if_key = "USB_NCM"` 固定で、`end()`経路は
netifを破棄しない）→ `end()` → `begin()` の順でhost stackを再起動する。`end()`は
`onDeviceDisconnected()`を呼ばないので、sketch側の状態は自前で戻す必要がある。

これはUSB仕様の制約ではなくESP-IDF APIの制約。USB仕様上は
`GET_DESCRIPTOR(CONFIGURATION, index)` はAddress stateでも全indexに応答する義務があり、
activeでないconfigurationのdescriptorも読める（`getNetworkInterfaces()` がそうしている）。
仕様が定めているのは「activeなconfigurationは1つ」「interfaceはactive configuration内の
ものしかclaimできない」だけ。2パスになるのは、`enum_filter_cb` が device descriptor しか
渡さず USB transfer 禁止（`usb/usb_types_stack.h`）、`usb_host_get_config_desc()` が
device handle 必須で列挙後専用、`usb_host.h` に列挙後のconfiguration変更APIが無い、という
3点の帰結。TinyUSBは無関係（host側はESP-IDF USB Host Library）。

詳細は `examples/UsbNetwork/README.ja.md` の「2パス必要な理由」「パス2を自動化する場合」。

open 時の処理:

1. candidate が active configuration 内にあることを確認
2. control interface と data interface を claim
3. data interface の alternate setting を有効化
4. endpoint と channel 使用量を記録

未実装:

- descriptor調査結果を保存して自動的に再enumerationする二段階選択
- notification interrupt IN transfer
- bulk IN transfer
- protocol ごとの初期化 control request

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

CDC-ECM/NCM の peer device を Arduino Core 標準機能だけで用意するのは難しい。兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceNet`（TinyUSB CDC-NCM ベース）が NCM device を提供するため、これと組み合わせた自動 peer test を用意する。

実装済み:

- `tests/peer/usb_ncm`
  - Host が CDC-NCM interface を列挙・open し、DHCP クライアントの lwIP netif として attach
  - peer device (`EspUsbDeviceNet`) は DHCP サーバ（`192.168.7.1`）と固定ページを提供
  - Host が `192.168.7.x` リースを取得し、`http://192.168.7.1/` を HTTP GET して固定 body を検証
  - device 自身の IP への on-link 通信なので、device 側 DHCP のデフォルト GW / DNS 設定の不足には依存しない

## 実装段階

### Phase 0: descriptor 調査

完了済み。

- `EspUsbHostNetworkInterfaceInfo`
- `getNetworkInterfaces()`
- `usb_network_descriptor` manual test
- CDC ACM と ECM/NCM の自動claim分離

### Phase 1: configuration selection / open

一部実装済み。

- `networkOpen()`
- `networkClose()`
- interface claim / release
- data alternate setting
- endpoint channel 使用量のログ

未実装:

- activeではないconfigurationの選択
- notification endpoint transfer
- bulk IN/OUT transfer

この段階では Ethernet frame はまだ lwIP に渡さない。

注意:
ESP-IDF の `usb_host_get_active_config_descriptor()` はenumeration時にcacheされたactive configurationを返す。実機AX88179Aで手動 `SET_CONFIGURATION` 後にinterface claimを試したところ、Host側のcached active descriptorが更新されず、config 2 の data interface claim が `ESP_ERR_NOT_FOUND` で失敗した。

ESP-IDFには `usb_host_config_t::enum_filter_cb` があり、enumeration開始時にconfiguration番号を選べる。ただしcallbackに渡されるのはdevice descriptorのみで、configuration descriptorは読めない。そのため、完全な汎用自動選択には以下のどちらかが必要。

- いったんdefault configurationでenumerationし、全configurationを調査した後、選択結果を保存してdeviceを再enumerationする
- ユーザーが事前にVID/PIDやconfiguration値を指定し、enum filterでそのconfigurationを選ぶ

AX88179A専用のVID/PID分岐にはしない。汎用実装としては、調査結果を保存して再enumerationする方式を優先検討する。

追加確認:
Arduino-ESP32 3.3.10 の ESP32-S3 build では `sdkconfig` に `# CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK is not set` があり、`usb_host_config_t::enum_filter_cb` は有効にならない。実機AX88179AでVID/PID指定によるconfiguration 2選択を試したが、deviceはconfig 1のまま列挙された。

その後、Arduino-ESP32側へ `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` を有効化するPRを出し、採用された。Arduino-ESP32 3.3.11で有効化され、`setConfigurationSelector()`から`enum_filter_cb`を使うconfiguration選択を実装した。

3.3.10時点では「activeではないconfigurationへ切り替えて、そのinterfaceをESP-IDF USB Host APIでclaimする」経路は標準Arduino core上で使えない。3.3.11以降ではselectorによる事前指定が利用できる。

AX88179Aの標準CDC-NCM/ECM configurationを開くには、`enum_filter_cb`を使い、事前調査済みのconfiguration値をenumeration時に選択する。ライブラリ本体にはAX88179A専用のVID/PID分岐を置かず、公開selector APIとして実装した。

### Phase 2: CDC-ECM raw frame

- raw frame callback / read API（`onNetworkFrame` / `networkReadFrame` / `networkWriteFrame`）実装済み。frame RX/TX 経路と RX ring は ECM/NCM 共通。
- ECM 固有の frame 境界（bulk short packet 単位）と link status notification は今後の実機確認対象。

raw frame API は NCM 経路で先に実装した（peer device が NCM のため）。

### Phase 3: CDC-NCM raw frame

実装済み。

- NTH16 / NDP16 の parse（`handleNetworkInput`）と 1 NTB 1 frame の build（`buildNcmFrame`）
- bulk IN（NTB 受信、サイズは device 毎に交渉。既定の希望値が `ESP_USB_HOST_NETWORK_NTB_IN_MAX=3200`）/ bulk OUT（同期送信）/ interrupt IN 通知
- 通知（NETWORK_CONNECTION / SPEED_CHANGE）による link 状態更新（`networkLinkUp`）
- 複数 datagram aggregation の受信は NDP chain / datagram table を辿って対応。送信は 1 datagram 固定。
- NTB 入力サイズの交渉（`negotiateNetworkNtbInput`、open 時に device 毎）:
  `GET_NTB_PARAMETERS`(0xA1/0x80) で `dwNtbInMaxSize` / `dwNtbOutMaxSize` を読み、
  NCM functional descriptor（subtype 0x1a）の `bmNetworkCapabilities` bit 3 が対応を示せば
  `SET_NTB_INPUT_SIZE`(0x21/0x86) で device 側を希望値まで下げる。非対応なら受信バッファを
  device の最大値に合わせて確保する（上限 `ESP_USB_HOST_NETWORK_NTB_IN_LIMIT`=16KB、`-D` 可）。
  決定値は bulk IN の max packet size の倍数へ切り下げる（ESP-IDF は IN 転送長を MPS の整数倍と
  規定。3200 は 64 の倍数だが 512 の倍数ではないので、High-Speed では 3072 になる）。
  結果は `EspUsbHostNetworkStats::ntbInSize`、超過で破棄した NTB 数は `rxOversized` で公開。

  この交渉が無いと何が起きるか（実測）: NCM では host が下げない限り `dwNtbInMaxSize` は device 任せで、
  device は負荷が上がってから複数 datagram を 1 NTB にまとめ始める。固定 3200 バッファを超える NTB を
  破棄していた版では、8192 を申告する device 相手に device→host が **0.098 Mbps・2.2 秒の TCP ストール**まで
  落ちた（同じ device を 3200 にすれば 3.20 Mbps）。リンクは up、`txFails` も 0 のままなので原因不明の
  パケットロスに見える。交渉後は同条件で 3.3〜4.4 Mbps・`rxOversized=0`。回帰は
  `tests/peer/usb_ncm_throughput` が押さえる（`usb_ncm` は 1 NTB 1 datagram しか作らず検出できない）。
  未検証: `SET_NTB_INPUT_SIZE` 側の分岐（capability bit 3 を申告する device が手元に無い。
  EspUsbDevice peer は 0 を申告する）と、High-Speed（ESP32-P4）での MPS 倍数の実機確認。

data interface は CDC-DATA class（0x0a）で CDC-ACM serial と同一のため、`handleTransfer` で network data endpoint を serial より先に分岐する。

実機知見（重要）: ESP32-S3 の Full Speed では、1つの NTB が **bulk IN の複数完了（USB パケット=64B 単位）にまたがって**届く。「1 完了 = 1 NTB」を前提にすると、先頭 64B パケット（NTH を含む）しか見えず datagram が blockLength 外になって全フレームを取りこぼす（`rxNtb>0` だが `rxFrames=0`）。そのため `handleNetworkInput` は device 毎の再アセンブルバッファにチャンクを追記し、`wBlockLength` 分たまってから `parseNetworkNtb` で解析する。ESP-IDF が 1 完了で NTB 全体を返す構成でも同じロジックで動く。

DHCP client 起動順序: netif は `action_start` → `action_connected`（リンク up）→ `dhcpc_start` の順で起動する必要がある（リンク up より先に dhcpc_start すると DISCOVER が送出されない）。診断用に `EspUsbHostNetworkStats` / `networkStats()`（rxNtb/rxFrames/tx/txFail/rxOversized/ntbInSize/link）を公開している。

### Phase 4: lwIP netif

実装済み。

- `esp_netif` に ETH netstack で netif 登録（`networkAttachNetif` / `networkDetachNetif` / `networkLocalIP`）
- transmit hook は `networkWriteFrame`（NTB 化して bulk OUT、transfer buffer にコピーするので呼び出し元 buffer の寿命に非依存）
- RX は `handleNetworkInput` → `esp_netif_receive`
- DHCP client / static IP（+ DNS）/ link up/down
- device 切断時に netif を detach（`handleDeviceGone`）
- EspUsbDevice レビュー由来の対策: attach 失敗時に `esp_netif_destroy`（リーク/キー再利用対策）
- host netif MAC は CDC Ethernet FD の `iMACAddress`（GET_DESCRIPTOR STRING → 12桁hex）をそのまま採用。実 USB NIC の標準動作に合わせる。`iMACAddress` 未提供時のみローカル管理 MAC にフォールバック。
  - デバイス自身が同じ MAC で IP スタックを動かすと point-to-point で衝突するため、デバイス側は自 netif MAC を広告値と別（1ビット反転）にする必要がある。EspUsbDevice は対応済み。
- 今後: descriptor調査後の自動再enumeration、複数 NIC 同時対応

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
