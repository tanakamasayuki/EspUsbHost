# EspUsbHost CCID (Smart Card Reader) API 仕様

> **English readers:** the design of the CCID smart card reader API plus the results that
> came out of implementing it against a Sony RC-S300 (`054c:0dc8`) — ATR handling, card
> type decoding, APDU exchange, escape commands and FeliCa transparent sessions. The
> current API is documented in "CCID smart card reader" in [README.md](../README.md), with
> runnable code in [`examples/Ccid/`](../examples/Ccid/).

> 状態: 実装済み。本文は設計時点の記述を残しつつ、実装で確定した点を「実装結果」節に追記している。

## 目的

`EspUsbHost` に、USB CCID (Integrated Circuit(s) Cards Interface Devices) クラスのスマートカードリーダーを扱うための汎用 API を追加する。

対象は `bInterfaceClass == 0x0b` の interface で、bulk OUT (PC_to_RDR) / bulk IN (RDR_to_PC) による CCID メッセージ交換、interrupt IN による slot 状態通知、EP0 の CCID class request に限定する。

特定リーダー専用のプロトコル (Sony PaSoRi の FeliCa 用 vendor command 等) は API 本体には入れず、`ccidEscape()` と既存 vendor API の上でユーザーが実装できる形にする。

## 実機で確認済みのターゲット

Sony RC-S300 (`FeliCa Port/PaSoRi 4.0`) を ESP32-S3 に接続して descriptor を取得した (`tests/manual/ccid_info`)。

```
DEVICE address=2 vid=054c pid=0dc8 class=0x00 subclass=0x00 protocol=0x00 speed=full interfaces=2 manufacturer="SONY" product="FeliCa Port/PaSoRi 4.0" serial="1274667"
  INTERFACE number=0 alt=0 class=0x0b subclass=0x00 protocol=0x00 endpoints=3 ccid=1
  INTERFACE number=1 alt=0 class=0xff subclass=0x00 protocol=0x00 endpoints=2
  ENDPOINT interface=0 address=0x81 dir=in  type=bulk      mps=64 interval=4
  ENDPOINT interface=0 address=0x01 dir=out type=bulk      mps=64 interval=4
  ENDPOINT interface=0 address=0x83 dir=in  type=interrupt mps=64 interval=255
  ENDPOINT interface=1 address=0x82 dir=in  type=bulk      mps=64 interval=4
  ENDPOINT interface=1 address=0x02 dir=out type=bulk      mps=64 interval=4
```

判明した事実。

- interface 0 が標準的な bulk CCID (subclass `0x00` / protocol `0x00`)。bulk IN + bulk OUT + interrupt IN の 3 endpoint 構成
- interface 1 は vendor-specific。CCID API の対象外で、必要なら既存の `vendorOpen()` 系で扱える
- device class は `0x00` なので、CCID の検出は interface descriptor だけで行う必要がある
- bulk MPS は 64 bytes (full speed)。CCID message は複数 packet に分割されて届く

CCID class descriptor (`bDescriptorType == 0x21`, 54 bytes) は現在 `EspUsbHost` が parse していないため、`dwFeatures` / `dwMaxCCIDMessageLength` / `bMaxSlotIndex` は未取得。実装フェーズで parse を追加する (後述)。

## 取り込む範囲

1. CCID interface の検出と claim、CCID class descriptor の parse
2. slot 状態取得 (`PC_to_RDR_GetSlotStatus`) とカード有無判定
3. カードの活性化・非活性化 (`IccPowerOn` / `IccPowerOff`) と ATR 取得
4. データ交換 (`PC_to_RDR_XfrBlock`) と、その上の APDU 送受信ヘルパ、ATR からのカード種別判定
5. `PC_to_RDR_Escape` によるベンダー固有コマンド送信
6. interrupt IN (`RDR_to_PC_NotifySlotChange`) によるカード挿入・排出コールバック
7. 上記に収まらないメッセージ用の汎用 raw message API
8. CCID class request の `ABORT`

## 取り込まない範囲

- PC/SC 互換レイヤ、reader name、SCardTransmit 相当の API 全体
- ICCD (interface protocol `0x01` / `0x02`) — bulk なしで control transfer だけを使う変種
- PIN pad / secure PIN entry (`PC_to_RDR_Secure`)、`Mechanical`、`SetDataRateAndClockFrequency`
- T=0 / T=1 の TPDU 分割をホスト側で行う character / TPDU exchange level のエミュレーション (初回は short APDU exchange level のリーダーを前提とし、それ以外は raw API に委ねる)
- ISO 7816 / ISO 14443 / FeliCa の上位プロトコル解釈
- RC-S300 の FeliCa polling 用 vendor command 列 (別途 example / manual test として扱う)
- 複数 CCID interface の同時 open (1 device 1 interface)

## 公開データ型

```cpp
// bStatus の下位 2 bit。
enum EspUsbHostCcidIccStatus : uint8_t {
  ESP_USB_HOST_CCID_ICC_ACTIVE = 0,    // カードあり、活性化済み
  ESP_USB_HOST_CCID_ICC_INACTIVE = 1,  // カードあり、非活性
  ESP_USB_HOST_CCID_ICC_ABSENT = 2,    // カードなし
};

// bStatus の上位 2 bit。
enum EspUsbHostCcidCommandStatus : uint8_t {
  ESP_USB_HOST_CCID_COMMAND_OK = 0,
  ESP_USB_HOST_CCID_COMMAND_FAILED = 1,
  ESP_USB_HOST_CCID_COMMAND_TIME_EXTENSION = 2,
};

// IccPowerOn の bPowerSelect。
enum EspUsbHostCcidVoltage : uint8_t {
  ESP_USB_HOST_CCID_VOLTAGE_AUTO = 0,
  ESP_USB_HOST_CCID_VOLTAGE_5V = 1,
  ESP_USB_HOST_CCID_VOLTAGE_3V = 2,
  ESP_USB_HOST_CCID_VOLTAGE_1V8 = 3,
};

// dwFeatures の bit 16..18 から求める exchange level。
enum EspUsbHostCcidExchangeLevel : uint8_t {
  ESP_USB_HOST_CCID_EXCHANGE_CHARACTER = 0,
  ESP_USB_HOST_CCID_EXCHANGE_TPDU = 1,
  ESP_USB_HOST_CCID_EXCHANGE_SHORT_APDU = 2,
  ESP_USB_HOST_CCID_EXCHANGE_EXTENDED_APDU = 3,
};

struct EspUsbHostCcidInterface {
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t inEndpoint = 0;             // bulk IN
  uint8_t outEndpoint = 0;            // bulk OUT
  uint8_t interruptEndpoint = 0;      // interrupt IN、無ければ 0
  uint16_t inMaxPacketSize = 0;
  uint16_t outMaxPacketSize = 0;
  // CCID class descriptor (0x21) 由来。descriptor が無い場合は既定値のまま。
  bool hasClassDescriptor = false;
  uint16_t bcdCCID = 0;
  uint8_t slotCount = 1;              // bMaxSlotIndex + 1
  uint8_t voltageSupport = 0;         // bVoltageSupport
  uint32_t protocols = 0;             // dwProtocols (bit0 = T=0, bit1 = T=1)
  uint32_t features = 0;              // dwFeatures
  uint32_t maxMessageLength = 0;      // dwMaxCCIDMessageLength
  uint8_t maxBusySlots = 1;           // bMaxCCIDBusySlots
  EspUsbHostCcidExchangeLevel exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_CHARACTER;
};

struct EspUsbHostCcidStatus {
  uint8_t address = 0;
  uint8_t slot = 0;
  EspUsbHostCcidIccStatus iccStatus = ESP_USB_HOST_CCID_ICC_ABSENT;
  EspUsbHostCcidCommandStatus commandStatus = ESP_USB_HOST_CCID_COMMAND_OK;
  uint8_t error = 0;                  // bError (commandStatus が FAILED のとき有効)
  bool present = false;               // iccStatus != ABSENT
  bool active = false;                // iccStatus == ACTIVE
};

struct EspUsbHostCcidSlotEvent {
  uint8_t address = 0;
  uint8_t slot = 0;
  bool present = false;               // 通知時点のカード有無
};

// raw message API の応答。
struct EspUsbHostCcidResponse {
  uint8_t messageType = 0;            // bMessageType
  uint8_t slot = 0;
  uint8_t sequence = 0;               // bSeq
  uint8_t status = 0;                 // bStatus の生値
  uint8_t error = 0;                  // bError
  uint8_t chainParameter = 0;         // bChainParameter / bClockStatus / bRFU
  EspUsbHostCcidIccStatus iccStatus = ESP_USB_HOST_CCID_ICC_ABSENT;
  EspUsbHostCcidCommandStatus commandStatus = ESP_USB_HOST_CCID_COMMAND_OK;
  const uint8_t *data = nullptr;      // abData。呼び出し完了までのみ有効
  size_t length = 0;
};
```

`EspUsbHostCcidResponse::data` は呼び出しから戻った後は無効。保持したい場合はコピーする。

## 公開 API

```cpp
using CcidSlotChangeCallback = std::function<void(const EspUsbHostCcidSlotEvent &)>;

// --- 検出 / open ---
bool ccidOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t interfaceNumber = 0xff);
void ccidClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool ccidReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool ccidGetInterface(EspUsbHostCcidInterface &info,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t ccidSlotCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// --- slot 状態 ---
bool ccidGetStatus(EspUsbHostCcidStatus &status,
                   uint8_t slot = 0,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = 1000);
bool ccidCardPresent(uint8_t slot = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

// --- カード活性化 ---
bool ccidPowerOn(uint8_t *atr = nullptr,
                 size_t atrCapacity = 0,
                 size_t *atrLength = nullptr,
                 EspUsbHostCcidVoltage voltage = ESP_USB_HOST_CCID_VOLTAGE_AUTO,
                 uint8_t slot = 0,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = 5000);
bool ccidPowerOff(uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = 2000);
size_t ccidGetAtr(uint8_t *buffer,
                  size_t capacity,
                  uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// --- データ交換 ---
bool ccidTransfer(const uint8_t *tx,
                  size_t txLength,
                  uint8_t *rx,
                  size_t rxCapacity,
                  size_t *rxLength,
                  uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = 5000);
bool ccidApdu(const uint8_t *apdu,
              size_t apduLength,
              uint8_t *response,
              size_t responseCapacity,
              size_t *responseLength,
              uint16_t *statusWord = nullptr,
              uint8_t slot = 0,
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint32_t timeoutMs = 5000);
bool ccidEscape(const uint8_t *tx,
                size_t txLength,
                uint8_t *rx,
                size_t rxCapacity,
                size_t *rxLength,
                uint8_t slot = 0,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = 5000);

// --- 汎用 raw message ---
bool ccidMessage(uint8_t messageType,
                 const uint8_t messageSpecific[3],
                 const uint8_t *data,
                 size_t length,
                 EspUsbHostCcidResponse &response,
                 uint8_t slot = 0,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = 5000);

// --- 制御 / 通知 ---
bool ccidAbort(uint8_t slot = 0,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = 1000);
void onCcidCardInserted(CcidSlotChangeCallback callback);
void onCcidCardRemoved(CcidSlotChangeCallback callback);
uint8_t ccidLastError(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;  // 直近の bError
```

CCID interface を持つ device の有無だけを知りたい場合は、既存の `getInterfaces()` で `interfaceClass == 0x0b` を探せば足りるため、専用の列挙 API は初回範囲に入れない。

## API 挙動

### `ccidOpen` / `ccidClose` / `ccidReady`

`bInterfaceClass == 0x0b` の interface を claim し、bulk IN / bulk OUT / interrupt IN endpoint を記録する。

- `address == ESP_USB_HOST_ANY_ADDRESS` のとき、最初に見つかった CCID device を使う
- `interfaceNumber == 0xff` のとき、最初に見つかった CCID interface を使う
- bulk IN と bulk OUT の両方が必要。片方でも欠ける interface は失敗扱い
- interrupt IN は任意。ある場合は継続 transfer を張り、slot 変化通知を受ける
- open 時に CCID class descriptor (`0x21`) を parse して `EspUsbHostCcidInterface` を埋める。descriptor が無い場合も open は成功させ、`hasClassDescriptor = false` として既定値 (slot 1 個、`maxMessageLength = 271`) で動作する
- claim 済み interface に対する再呼び出しは成功扱い
- `ccidClose()` は interrupt transfer を止め、interface を release する。device 切断時は内部状態を自動でリセットする
- subclass / protocol が `0x00` 以外 (ICCD) の interface は初回範囲外として open を失敗させ、ログに理由を出す

vendor interface との自動 claim 衝突を避けるため、既存の vendor API と同じく自動 claim はせず、`ccidOpen()` の明示呼び出しで claim する。

### bSeq とメッセージ整合

- `bSeq` はライブラリが device ごとに管理し、コマンド毎に 1 加算する
- 応答は `bSeq` が一致するものだけを採用する。一致しない応答は破棄してカウンタに記録する
- 1 つの CCID interface に対するコマンドは内部 mutex で直列化する。`bMaxCCIDBusySlots` を超える並行コマンドは発行しない

### タイムアウトと Time Extension

- `bStatus` の command status が `TIME_EXTENSION (2)` の応答を受けたら、その応答は最終応答ではないので待ち続ける。待ち時間は `bError` (BWI 倍率) に応じて延長し、`timeoutMs` を上限とする
- タイムアウト時は bulk IN / bulk OUT を halt / flush して完了 callback を待ち、HCD が transfer を返却してから解放する (既存 `vendorWrite()` と同じ手順)
- タイムアウト後は `ccidAbort()` を内部で発行して slot を回復させる

### 分割受信とチェイン

- bulk IN は MPS 64 bytes 単位で届くため、10 bytes header の `dwLength` を見て全 message が揃うまで結合する
- 応答が `dwMaxCCIDMessageLength` を超える場合の `bChainParameter` によるチェインは、初回実装では extended APDU レベルのみ発生する。short APDU レベルのリーダーが対象のため、初回はチェインを検出したら `false` を返してログを出す (対応は後続)

### `ccidGetStatus` / `ccidCardPresent`

`PC_to_RDR_GetSlotStatus (0x65)` を送り、`RDR_to_PC_SlotStatus (0x81)` の `bStatus` を分解して返す。

- `ccidCardPresent()` は `ccidGetStatus()` の `present` を返す簡易版。失敗時は `false`
- interrupt IN が使えるリーダーでは、通知で得た最新状態をキャッシュし、ポーリングを減らせる。ただし `ccidGetStatus()` は常に実際のコマンドを発行する

### `ccidPowerOn` / `ccidPowerOff` / `ccidGetAtr`

`PC_to_RDR_IccPowerOn (0x62)` / `PC_to_RDR_IccPowerOff (0x63)`。

- `ccidPowerOn()` の応答 `RDR_to_PC_DataBlock (0x80)` の `abData` が ATR。`atr != nullptr` のとき `atrCapacity` までコピーし、`atrLength` に実長を返す
- ATR は内部にもキャッシュし、`ccidGetAtr()` で後から取得できる。`ccidPowerOff()` とカード排出でクリアする
- `voltage == AUTO` は `bPowerSelect = 0`。失敗した場合に 5V → 3V → 1.8V を自動リトライするかは実装時に決める (初回は自動リトライなし)
- command status が `FAILED` の応答は `false` を返し、`bError` を `ccidLastError()` に記録する。カード無し (`ICC_MUTE` 等) と通信エラーの区別は `bError` で行う

### `ccidTransfer`

`PC_to_RDR_XfrBlock (0x6f)` を送り、`RDR_to_PC_DataBlock (0x80)` の `abData` を返す。

- `bBWI = 0` (既定の BWT)、`wLevelParameter = 0` を使う
- `txLength` が `dwMaxCCIDMessageLength - 10` を超える場合は `false`
- 受信データが `rxCapacity` を超える場合は `false` とし、`rxLength` に必要サイズを返す
- 送受信内容は解釈しない。TPDU でも APDU でもそのまま透過させる

### `ccidApdu`

`ccidTransfer()` の薄いラッパーで、応答の末尾 2 bytes を SW1SW2 として切り出す。

- `statusWord != nullptr` のとき `(SW1 << 8) | SW2` を返す
- `response` / `responseLength` には SW を除いたデータ部だけを返す
- 応答が 2 bytes 未満なら `false`
- `61 xx` (GET RESPONSE 要求) と `6c xx` (Le 訂正) の自動追従は初回範囲外とし、必要ならユーザーが `ccidApdu()` を再発行する

### `ccidEscape`

`PC_to_RDR_Escape (0x6e)` を送り、`RDR_to_PC_Escape (0x83)` の `abData` を返す。RC-S300 の FeliCa 用コマンドなど、ベンダー固有の拡張はこの API を使う。

### `ccidMessage`

上記で表現できない CCID メッセージ (`SetParameters`, `GetParameters`, `IccClock`, `T0APDU`, `Mechanical` など) を送るための raw API。

- `messageType` は `PC_to_RDR_*` の値
- `messageSpecific` は header の 7..9 byte (3 bytes)。`nullptr` のときはゼロ
- 応答は `EspUsbHostCcidResponse` にそのまま入れる。`data` は次の CCID 呼び出しまで有効
- この API があることで、ライブラリが未対応のメッセージでもユーザー側で実装できる (「汎用的に使える」ための逃げ道)

### `ccidAbort`

CCID class request `ABORT (bRequest = 0x01, bmRequestType = 0x21, wValue = bSlot | (bSeq << 8), wIndex = interface)` を送り、続けて `PC_to_RDR_Abort (0x72)` を送って `RDR_to_PC_SlotStatus` を待つ、という CCID 仕様の手順を実装する。

`GET_CLOCK_FREQUENCIES (0x02)` / `GET_DATA_RATES (0x03)` は初回範囲外。

### `onCcidCardInserted` / `onCcidCardRemoved`

interrupt IN の `RDR_to_PC_NotifySlotChange (0x50)` を解釈する。

- `bmSlotICCState` は slot あたり 2 bit (bit0 = 現在の有無、bit1 = 前回からの変化)
- 変化 bit が立った slot について、present なら inserted、absent なら removed の callback を呼ぶ
- callback は USB task 文脈で呼ばれる。重い処理や同期 CCID コマンドの発行は禁止 (要求をフラグに立てて `loop()` で処理する)
- `RDR_to_PC_HardwareError (0x51)` はログに出し、slot 状態を absent 扱いにリセットする

### 呼び出し文脈

`ccidGetStatus()` / `ccidPowerOn()` / `ccidTransfer()` / `ccidApdu()` / `ccidEscape()` / `ccidMessage()` / `ccidAbort()` は転送完了を同期的に待つ。USB task の callback から呼んだ場合は `false` を返す。既存の `vendorWrite()` / MSC API と同じ規約にする。

## 内部実装方針

- `DeviceState` に CCID 用フィールドを追加する (interface number、endpoint、MPS、class descriptor 由来の値、bSeq、ATR キャッシュ、slot 状態キャッシュ、mutex、interrupt transfer)
- 受信バッファは `ESP_USB_HOST_CCID_BUFFER_SIZE` (既定 512 bytes、ビルドフラグで上書き可) を device ごとに 1 つ。short APDU レベルの `dwMaxCCIDMessageLength` は 271 前後なので十分
- CCID を使わないスケッチのフットプリント増を抑えるため、大きいバッファは `ccidOpen()` 時に確保し `ccidClose()` / 切断で解放する
- `parseConfigDescriptor()` に `bDescriptorType == 0x21` の CCID class descriptor 取り込みを追加する (HID の `0x21` と衝突しないよう、interface class が `0x0b` のときだけ CCID として解釈する)
- 実装は `src/EspUsbHost.cpp` (12k 行) をこれ以上肥大させないため、`src/EspUsbHostCcid.h` / `src/EspUsbHostCcid.cpp` に分ける。公開型と `EspUsbHost` のメンバ宣言は `EspUsbHost.h` に置き、定義を `EspUsbHostCcid.cpp` に置く (`EspUsbHostHid.*` と同じ構成)

## テスト方針

### 追加済み

- `tests/manual/ccid_info` — CCID interface と endpoint の列挙。実機の descriptor を確認する probe

### 実装フェーズで追加

- `tests/manual/ccid_slot_status` — `ccidOpen()` → `ccidGetStatus()`、カードの有無が正しく出ること。カードを外して再実行すると `present=0` になること
- `tests/manual/ccid_power_on` — `ccidPowerOn()` で ATR が取得できること (接触/非接触どちらでも ATR 相当が返る)
- `tests/manual/ccid_apdu` — PC/SC 疑似 APDU `FF CA 00 00 00` (Get UID) を `ccidApdu()` で送り、UID と SW `9000` が返ること
- `tests/manual/ccid_hotplug` — カード抜き差しで `onCcidCardInserted()` / `onCcidCardRemoved()` が発火すること (interrupt IN を持つリーダーのみ)

自動テスト (peer) は、`EspUsbDevice` 側に CCID device 実装が無いため当面は作らない。必要なら後続で `EspUsbDeviceCcid` の要否を検討する。

### RC-S300 の実測結果

`tests/manual/ccid_card` で確認済み。

```
CCID_INTERFACE address=2 iface=0 in=0x81 out=0x01 interrupt=0x83 classDesc=1 bcd=0110 slots=1 voltage=0x07 protocols=0x00000002 features=0x0004007e maxMessage=522 exchange=3
CCID_STATUS slot=0 icc=active present=1 active=1 command=0 error=0x00
CCID_ATR data=3b8f8001804f0ca000000306030001000000006a
CCID_APDU attempt=0 sw=9000 length=4
CCID_UID data=6b6dccae
```

- 標準 CCID の `IccPowerOn` / `XfrBlock` だけで ISO 14443 カードの APDU が通る。Sony 固有の escape は不要だった
- exchange level は extended APDU (`dwFeatures` bit 18)、protocol は T=1、slot は 1 個、`dwMaxCCIDMessageLength` は 522
- ATR は PC/SC の合成 ATR (RID `A000000306`、SS=03 = ISO 14443 A part 3)
- Get UID (`FF CA 00 00 00`) を 3 回繰り返しても同じ UID と `9000` が返り、その後の生 `GetSlotStatus` も `bSeq` が同期したまま成功する
- `ccidGetCardInfo()` はこの ATR を `ISO 14443 A` level 3 / `MIFARE Classic 1K` (PIX.SS=0x03、PIX.Name=0x0001) と判定する
- カードを外して戻すと `onCcidCardRemoved()` → `onCcidCardInserted()` が 1 回ずつ発火する (`tests/manual/ccid_hotplug`)
- FeliCa 本来のプロトコルは ISO 7816 APDU ではないため、FeliCa のブロック読み書き (Read Without Encryption 等) は標準 APDU では行えない。IDm の取得までは下記のとおり標準経路で可能

FeliCa カードを載せた場合の実測。

```
CCID_ATR      data=3b8f8001804f0ca00000030611003b0000000042
CCID_CARD     standard="FeliCa" code=0x11 level=0 name="FeliCa" nameCode=0x003b pcsc=1 protocols=0x03
CCID_IDENTIFY standard="FeliCa" fromUid=0 uidLength=0
CCID_APDU     attempt=0 sw=9000 length=8
```

- RC-S300 は CCID モードで FeliCa を扱い、PC/SC 合成 ATR の PIX.SS=0x11 / PIX.Name=0x003b で FeliCa と申告する。ATR だけで判定できるため `ccidIdentifyCard()` の UID フォールバックは動かない (`fromUid=0`)
- Get UID (`FF CA 00 00 00`) は 8 byte の IDm を返す
- 仕様書だけを根拠にしていた PIX.SS=0x11 と PIX.Name=0x003b の対応が実機で裏付けられた

iPhone (Apple Pay) を載せた場合の実測。

iPhone (Apple Pay) を載せた場合の実測。

```
CCID_ATR      data=3b80800101
CCID_CARD     standard="unknown" pcsc=0
CCID_IDENTIFY standard="ISO 14443 (type A or B)" fromUid=1 uidLength=4 uid=08391eaf
```

- ATR は `3b 80 80 01 01` で historical bytes が 0 byte。ATR にカード識別情報が一切入らないケースがあることが分かり、これを `ISO 7816 card` と誤判定しないよう `unknown` を返す実装に修正した
- Get UID は 4 byte の `08391eaf` を返した。先頭 `0x08` は ISO 14443-3 がランダム NFCID1 用に予約している値で、iPhone は FeliCa ではなく ISO 14443 Type A として応答している。この結果を受けて、4 byte かつ先頭 `0x08` は ISO 14443 A と判定するようにした (上記ログはその修正前のもの)
- iPhone のこの応答はリーダー側の制限ではない。同じリーダーに FeliCa カードを載せると上記のとおり FeliCa として識別されるため、iPhone 側が ISO 14443 Type A で応答していたということ
- 後日追記: この iPhone から Suica の IDm を取る方法は `examples/Ccid/EspUsbHostCcidFelicaIdm` で確立した。リーダー自前のポーリングでは Type A のランダム NFCID1 しか返らないが、RC-S300 の transparent session で RF フィールドを奪って FeliCa の Polling を自分で撃つと、同じ端末から 8 byte の IDm と応答元 System Code `0x0003` が返る。設計方針どおりライブラリ本体には手を入れず、`ccidTransfer()` の上に example 内で実装している (実測は `tests/probe/rcs300_felica`)

## README / ドキュメント反映方針

- README の対応表に「CCID smart card reader」を追加する
- API セクションは MSC / Vendor bulk と同列に「CCID」として追加する
- `docs/COMPATIBILITY.*.md` に RC-S300 の検証結果を追記する

## 実装結果

設計からの差分は以下。

- `ccidClose()` は CCID の動作停止とメッセージバッファ解放までとし、interface の claim は切断まで維持する。in-flight の interrupt transfer を安全に畳んでから release する手段が現状のライブラリにないため。再度 `ccidOpen()` すると claim 済みの interface と既存の endpoint slot を再利用する
- 実装ファイルは `src/EspUsbHostCcid.cpp`。CCID class descriptor の parse だけは `EspUsbHost.cpp` の `handleDescriptor()` 内 `0x21` の case に相乗りしている (HID descriptor と同じ番号なので interface class で分岐)
- manual test は設計時の 4 本 (`ccid_slot_status` / `ccid_power_on` / `ccid_apdu` / `ccid_hotplug`) ではなく 3 本にした。前 3 者は 1 回の実行で連続して確認できるため `ccid_card` にまとめている
- `ccidGetStatus()` は command status が FAILED の応答でも `true` を返す。カードが無い slot に対して失敗を返すリーダーがあり、その場合でも ICC status のビットは有効なため
- メッセージバッファは `dwMaxCCIDMessageLength` が既定の 512 を超える場合に 4096 まで拡張する (RC-S300 は 522)
- 設計時になかった `ccidIdentifyCard()` を追加した。ATR で判定できないカード向けに Get UID (`FF CA 00 00 00`) を送り、識別子の形から規格を推定する (8 byte は FeliCa の IDm、ただし先頭 `0xe0` は ISO 15693 の UID。7 / 10 byte は ISO 14443 A の NFCID1。4 byte で先頭 `0x08` は ISO 14443-3 がランダム NFCID1 用に予約している値。それ以外の 4 byte は NFCID1 と PUPI が同長のため type を確定しない)。推測であることは `info.fromUid` で区別できる
- 設計時になかった `ccidGetCardInfo()` を追加した。カードの種類 (ISO 14443 A/B、ISO 15693、FeliCa、低周波非接触、ISO 7816-10 メモリカード) とレベル、カード名を ATR から取り出す。非接触のストレージカードは自前の ATR を持たず、PC/SC 準拠のリーダーが historical bytes に PC/SC の RID `A0 00 00 03 06` と標準バイト (PIX.SS)・カード名 (PIX.Name) を載せた合成 ATR を作るため、そこから判定できる。自前の ATR を返すカード (接触カード、ISO 14443-4 で話す非接触カード) はこの識別情報を持たないので、推測せず `ISO 7816 card (own ATR)` として報告する
- ATR パーサは Arduino / USB 非依存の `src/EspUsbHostCcidAtr.h` に置き、host unit test `tests/unit/ccid_atr` が出荷するヘッダをそのまま g++ でコンパイルして検証する。カード名は PC/SC の代表的な値だけ名前を解決し、それ以外は生のコードを `cardName` で返す (未確認の名前を表示しないため)

## 保留事項

- `61 xx` / `6c xx` の自動追従を将来 API に入れるか (`ccidApduAuto()` のような別名にするか)
- extended APDU / chaining への対応時期
- `GetParameters` / `SetParameters` を型付き API にするか、`ccidMessage()` のままにするか
- 複数 slot リーダーでの slot 状態キャッシュの公開方法 (`ccidSlotPresent(slot)` を足すか)
- interrupt IN を持たないリーダー向けに、ライブラリ側でポーリングして挿抜 callback を出す仕組みを入れるか
- ICCD (protocol 0x01 / 0x02) 対応の要否
- transparent session (PC/SC part 3) を型付き API としてライブラリに入れるか。現状は `examples/Ccid/EspUsbHostCcidFelicaIdm` が RC-S300 の方言として example 内に持っている。データオブジェクト自体は PC/SC 標準なので共通化の余地はあるが、検証できたリーダーが 1 機種しかないため保留
