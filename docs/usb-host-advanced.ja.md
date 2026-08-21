# USB Host開発ガイド（上級編）

> English: [usb-host-advanced.md](usb-host-advanced.md)

[USB Host開発ガイド](usb-host-guide.ja.md)の続きです。入門編が「動かすまで」を扱うのに対し、こちらは**なぜそう動くのか、限界はどこか、限界にぶつかったときに何を測るのか**を扱います。

対象読者は、すでにデバイスを1つ以上動かしたことがあり、次のいずれかに直面している人です。

- ディスクリプタやキャプチャをバイト単位で読む必要がある
- エンドポイントのclaimに失敗する、スループットが足りない、転送が止まる
- 未対応クラスのラッパーを自分で書く
- コールバックのどこで何をしてよいのかを正確に知りたい

## 目次

1. [アーキテクチャとタスクモデル](#1-アーキテクチャとタスクモデル)
2. [ディスクリプタをバイト単位で読む](#2-ディスクリプタをバイト単位で読む)
3. [コントロール転送の解剖](#3-コントロール転送の解剖)
4. [転送のタイミングと帯域](#4-転送のタイミングと帯域)
5. [エンドポイント資源：チャネルとFIFO](#5-エンドポイント資源チャネルとfifo)
6. [エラーとリカバリ](#6-エラーとリカバリ)
7. [スループット設計](#7-スループット設計)
8. [コールバックのコンテキスト](#8-コールバックのコンテキスト)
9. [新しいクラス／プロトコルを実装する](#9-新しいクラスプロトコルを実装する)
10. [計測とデバッグ](#10-計測とデバッグ)

---

## 1. アーキテクチャとタスクモデル

### 1.1 層構造

```
スケッチ（loop / setup）
  ↕ コールバック登録・送信API
EspUsbHost              … クラスドライバ群、デバイス状態、受信バッファ
  ↕ usb_host_* API
ESP-IDF USB Host Library … デーモン、クライアント、列挙、外部ハブドライバ
  ↕ HCD
HCD (DWC OTG)           … チャネル割り当て、FIFO、URBスケジューリング
  ↕
USB OTGコントローラ + PHY
```

「Arduinoの設定では変えられない」制約（[入門編 3.3](usb-host-guide.ja.md#33-arduinoのビルド設定に由来する制限)）は、下2層がArduino-ESP32のビルド済みバイナリだからです。上2層はソースがあるので変更できます。

### 1.2 2つのタスク

`begin()` は**2つのFreeRTOSタスク**を作ります。

| タスク | 名前 | 役割 |
|--------|------|------|
| デーモン | `EspUsbHost` | `usb_host_lib_handle_events()` を回す。ライブラリのインストール、列挙、デバイスの生成・破棄 |
| クライアント | `EspUsbHostClient` | `usb_host_client_handle_events()` を5msタイムアウトで回す。**転送完了コールバックとこのライブラリのコールバックはここで走る** |

両方とも `EspUsbHostConfig` の `taskStackSize`（既定8192）、`taskPriority`（既定5）、`taskCore`（既定 `tskNO_AFFINITY`）で作られます。

```cpp
EspUsbHostConfig config;
config.taskStackSize = 12288;   // コールバックで重い処理をするなら増やす
config.taskPriority  = 6;       // 取りこぼしが気になるなら上げる
config.taskCore      = 1;       // Wi-Fiと分けたいときに固定
usb.begin(config);
```

クライアントタスクは、イベント処理のほかに毎周回で次を行います。ここを知っていると、遅延の出どころが読めます。

- 切断済みデバイスの後始末（`disconnectPending`）
- キーボードLEDレポートの送信。連続更新をまとめるため、変化から**20ms**待ってから送る
- エラーで止まったエンドポイントの復旧（[6章](#6-エラーとリカバリ)）
- IN転送の再サブミット

つまり**周期の下限は5ms**であり、コールバック内で長時間ブロックすると、この全部が止まります。

### 1.3 IN転送は常時サブミットされている

interrupt IN（キーボード、マウス、CCID通知など）は、ライブラリがURBを常に1本立てたままにし、完了→処理→即再サブミット、という形で回します。`managedEndpointCount()` が「常時受信転送を持つエンドポイント」の数です。

一方、bulk INには2つのモードがあります（`vendorOpen()` の `readMode`）。

| モード | 挙動 | 向く相手 |
|--------|------|---------|
| `ESP_USB_HOST_VENDOR_READ_CONTINUOUS` | 常にIN転送を立てて受信バッファ（既定512バイト、`ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE`）に溜める | 自発的に流してくる機器 |
| `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` | `vendorReadSync()` が呼ばれたときだけ転送する | 要求／応答型。BOTのようにトランザクション外のINがエラーになる機器 |

ここを間違えると「タイムアウトする」「エラーログが出続ける」という症状になります。

### 1.4 列挙への介入

`setConfigurationSelector()` はESP-IDFの `enum_filter_cb` を使い、**列挙の途中で有効化するコンフィグレーションを選びます**。既定のコンフィグにお目当ての機能がないUSB Ethernetアダプタなどで必要です。

- `begin()` の前に登録する
- USB Hostタスク上で走るので**ブロックしてはいけない**
- arduino-esp32 3.3.11以上が必要（それ未満では `false` と `ESP_ERR_NOT_SUPPORTED`）
- 1プロセスで1インスタンスだけが所有できる（2つ目は `ESP_ERR_INVALID_STATE`）

`0` を返せばデバイス既定のままです。どのコンフィグに何があるかは、いったん既定で列挙して [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) や [`usb_network_descriptor`](../tests/manual/usb_network_descriptor/) で全コンフィグを調べてから決めます（実質2パスの列挙になります）。

### 1.5 停止

`end()` はこのインスタンスがマウントしたMSCボリュームをアンマウントし、クライアントとデーモンを同期的に止め、実行中の転送をキャンセルして排出し、クライアントを登録解除し、IDFの `ALL_FREE` ハンドシェイクを待ってからホストライブラリをアンインストールします。アンインストールはIDFが受け付けるまでライブラリイベントを処理しながら再試行します。`NO_CLIENTS` などのイベントフラグが未読の間はIDFが拒否し、device listが空の場合は他に消費するものがないためです。戻った後は同じオブジェクトで `begin()` を再開できます。**USBコールバックの中から呼んではいけません**（アプリケーションタスクから呼ぶ）。

---

## 2. ディスクリプタをバイト単位で読む

[`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) や [`raw_descriptor`](../tests/manual/raw_descriptor/) が出す生バイトを、そのまま読めるようにするための表です。USBの多バイト値は**すべてリトルエンディアン**です。

### 2.1 デバイスディスクリプタ（18バイト）

| オフセット | サイズ | フィールド | 意味 |
|-----------|--------|-----------|------|
| 0 | 1 | bLength | 18 |
| 1 | 1 | bDescriptorType | 0x01 |
| 2 | 2 | bcdUSB | USBバージョン（0x0200 = USB 2.0） |
| 4 | 1 | bDeviceClass | 0x00ならインターフェース側で決まる |
| 5 | 1 | bDeviceSubClass | |
| 6 | 1 | bDeviceProtocol | |
| 7 | 1 | bMaxPacketSize0 | EP0のパケットサイズ（LS:8 / FS:8,16,32,64 / HS:64） |
| 8 | 2 | idVendor | VID |
| 10 | 2 | idProduct | PID |
| 12 | 2 | bcdDevice | デバイスのリビジョン。ファーム差の判別に使える |
| 14 | 1 | iManufacturer | 文字列インデックス |
| 15 | 1 | iProduct | 文字列インデックス |
| 16 | 1 | iSerialNumber | 文字列インデックス |
| 17 | 1 | bNumConfigurations | コンフィグの数。**2以上なら要注意** |

### 2.2 コンフィグレーションディスクリプタ（9バイト＋後続）

| オフセット | サイズ | フィールド | 意味 |
|-----------|--------|-----------|------|
| 0 | 1 | bLength | 9 |
| 1 | 1 | bDescriptorType | 0x02 |
| 2 | 2 | wTotalLength | **後続を含む総バイト数。256を超えるとESP32では列挙できない** |
| 4 | 1 | bNumInterfaces | |
| 5 | 1 | bConfigurationValue | SET_CONFIGURATIONに渡す値 |
| 6 | 1 | iConfiguration | |
| 7 | 1 | bmAttributes | bit6=セルフパワー, bit5=リモートウェイクアップ |
| 8 | 1 | bMaxPower | **2mA単位**（0x32 = 100mA） |

この9バイトの後ろに、インターフェース／エンドポイント／クラス固有のディスクリプタが**連結して並びます**。走査は「先頭バイトが長さ、2バイト目が型」を繰り返すだけです。

### 2.3 インターフェースディスクリプタ（9バイト）

| オフセット | フィールド | 備考 |
|-----------|-----------|------|
| 2 | bInterfaceNumber | `vendorOpen()` に渡す番号 |
| 3 | bAlternateSetting | **0以外は SET_INTERFACE で切り替える必要がある** |
| 4 | bNumEndpoints | alt=0では0本のことがある（オーディオが典型） |
| 5–7 | bInterfaceClass / SubClass / Protocol | 「何であるか」 |

オーディオやビデオでは、帯域が必要な設定が `bAlternateSetting >= 1` に置かれ、alt=0はエンドポイントを持ちません。これは「使わないときは帯域を予約しない」ための仕組みです。

### 2.4 エンドポイントディスクリプタ（7バイト）

| オフセット | フィールド | 備考 |
|-----------|-----------|------|
| 2 | bEndpointAddress | bit7=方向（1=IN）、bit3:0=番号 |
| 3 | bmAttributes | bit1:0 = 0:control 1:iso 2:bulk 3:interrupt。isoではbit3:2が同期種別、bit5:4が用途 |
| 4 | wMaxPacketSize | **bit10:0がサイズ、bit12:11はHSの追加トランザクション数**（高帯域iso/interrupt） |
| 6 | bInterval | ポーリング間隔。解釈は速度で変わる（[4.2](#42-bintervalの読み方)） |

`wMaxPacketSize` の上位ビットを無視して `0x0400`（=1024）と読むだけでは足りない場面があります。高帯域のisoでは `(1 + bit12:11)` 倍のトランザクションが1マイクロフレームで走ります。

### 2.5 クラス固有ディスクリプタ

`bDescriptorType` が 0x21以上のブロックは、そのインターフェースのクラスが定義する構造です。解析済みダンプには出ないので、生バイトを読む必要があります。

| 型 | 例 |
|----|-----|
| 0x21 | HIDディスクリプタ（レポートディスクリプタの長さと型を含む）／CDC・プリンタでは別の意味 |
| 0x22 | HIDレポートディスクリプタ（本体はGET_DESCRIPTORで別途取得） |
| 0x24 | CS_INTERFACE。CDCの機能ディスクリプタ、UACのユニット、CCIDのクラスディスクリプタ |
| 0x25 | CS_ENDPOINT。UACのエンドポイント属性など |
| 0x0b | IAD（複数インターフェースを1機能としてまとめる） |
| 0x29 | ハブディスクリプタ |

### 2.6 文字列ディスクリプタ

文字列は**インデックス参照**で、実体は別要求です。`GET_DESCRIPTOR(type=0x03, index=0)` が対応言語IDのリスト（通常 0x0409 = en-US）、`index=n, wIndex=langid` が本体で、中身はUTF-16LEです。

コンソールで直接読む場合:

```
ctl 80 06 0300 0000 ff      # 言語IDリスト
ctl 80 06 0302 0409 ff      # iProduct=2 の文字列
```

### 2.7 HIDレポートディスクリプタ

HIDだけは「データの意味」を別のディスクリプタで宣言します。構造はitemの列で、各itemの先頭バイトが `bTag(bit7:4) | bType(bit3:2) | bSize(bit1:0)`（sizeは0,1,2,4バイト）です。

| 種別 | 主なitem | 役割 |
|------|---------|------|
| Global | Usage Page, Report Size, Report Count, Report ID, Logical Min/Max | 以降に効く設定 |
| Local | Usage, Usage Min/Max | 直後のMainにだけ効く |
| Main | Input, Output, Feature, Collection, End Collection | 実際のフィールドを定義する |

読み方の要点は**「Report Size × Report Count が、そのMain itemが占めるビット数」**であることです。Report IDが宣言されている場合、レポートの先頭1バイトがIDになります。`onHIDInput()` が返す生バイトと突き合わせれば、どのビットが何かを確定できます。

Usage Pageが `0xff00` 以上（ベンダー定義）なら、中身は独自プロトコルです。[入門編 Step 5以降](usb-host-guide.ja.md#step-5-公開情報を探す)へ進みます。取得と簡易デコードは [`EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/)、生バイトの整形は `espUsbHostPrintHIDReportDescriptor()` が行います。

---

## 3. コントロール転送の解剖

### 3.1 setupパケットの8バイト

すべてのコントロール転送は、この8バイトで始まります。コンソールの `ctl` コマンドの引数と1対1に対応します。

| バイト | フィールド | 内容 |
|--------|-----------|------|
| 0 | bmRequestType | bit7=方向(1=IN) / bit6:5=型(0:標準 1:クラス 2:ベンダー) / bit4:0=宛先(0:デバイス 1:インターフェース 2:エンドポイント) |
| 1 | bRequest | 要求番号 |
| 2–3 | wValue | 要求ごとの意味 |
| 4–5 | wIndex | インターフェース番号やエンドポイントアドレスを入れることが多い |
| 6–7 | wLength | データステージのバイト数 |

よく使う `bmRequestType` の組み合わせ:

| 値 | 意味 |
|----|------|
| `0x80` | 標準・デバイス宛・IN（GET_DESCRIPTORなど） |
| `0x00` | 標準・デバイス宛・OUT（SET_CONFIGURATIONなど） |
| `0x02` | 標準・エンドポイント宛・OUT（CLEAR_FEATUREでhalt解除） |
| `0x21` / `0xa1` | クラス・インターフェース宛・OUT / IN（HID SET_REPORT、USBTMC、CCIDなど） |
| `0x40` / `0xc0` | ベンダー・デバイス宛・OUT / IN |

標準要求の番号: `GET_STATUS=0`, `CLEAR_FEATURE=1`, `SET_FEATURE=3`, `SET_ADDRESS=5`, `GET_DESCRIPTOR=6`, `GET_CONFIGURATION=8`, `SET_CONFIGURATION=9`, `GET_INTERFACE=10`, `SET_INTERFACE=11`。

### 3.2 3つのステージ

コントロール転送は Setup → （Data）→ Status の3ステージです。デバイスが要求を受け付けられなければ**STALL**を返し、ホスト側では転送エラーになります。**STALLは故障ではなく「その要求はサポートしていない」という回答**です。`ctl` が失敗したら、宛先（デバイス／インターフェース）と `wIndex` を疑ってください。

### 3.3 EP0はデバイス共有

EP0はインターフェースではなくデバイスに属します。このため `vendorControlTransfer()` は `vendorOpen()` なしで呼べます（実装上も、vendorデバイスが見つからなければ通常のデバイス検索にフォールバックします）。ライブラリがclaim済みのインターフェースを持つデバイスに対しても、EP0要求は送れます。

### 3.4 256バイトの壁（再掲）

`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` により、1転送は setup 8バイト＋データで256バイトまでです。実装では `usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, ...)` を呼ぶので、**データは248バイトが上限**になります。長いディスクリプタは分割取得が必要ですが、**列挙時にIDF側が一括で読む経路には分割がない**ため、`wTotalLength > 256` のデバイスはそもそも列挙できません。

---

## 4. 転送のタイミングと帯域

### 4.1 フレームとマイクロフレーム

| 速度 | 単位 | 長さ |
|------|------|------|
| LS / FS | フレーム | 1 ms |
| HS | マイクロフレーム | 125 µs |

ホストはこの単位でバスを分割し、周期転送（interrupt / iso）を先に、残りをbulkに割り当てます。周期転送はフレームの最大90%までしか予約できません。

### 4.2 bIntervalの読み方

| 速度・種別 | 解釈 |
|-----------|------|
| FS interrupt | そのままミリ秒（1〜255） |
| LS interrupt | 10〜255 ms |
| FS iso | `2^(bInterval-1)` フレーム（通常1） |
| HS interrupt / iso | `2^(bInterval-1)` マイクロフレーム（bInterval=4 なら 8×125µs = 1ms） |

「キーボードのポーリングが遅い」と感じたら、まずここを見ます。ただし**bIntervalは要求であって保証ではありません**。実際の間隔はホスト側のスケジューリングとチャネル競合に左右されます。

### 4.3 最大パケットサイズの制約

| 転送 | LS | FS | HS |
|------|----|----|----|
| Control | 8 | 8/16/32/64 | 64 |
| Bulk | — | 8/16/32/64 | **512固定** |
| Interrupt | ≤8 | ≤64 | ≤1024 |
| Isochronous | — | ≤1023 | ≤1024（高帯域で×3） |

HSの1024バイトinterrupt OUTがFSポートで開けないのは、そもそもFSに1024という選択肢がないためです（[入門編 3.2](usb-host-guide.ja.md#32-fsポートとhsポートの使い分けp4)）。

### 4.4 理論帯域と実測

| | 理論上限 | このライブラリの実測（bulk OUT） |
|---|---------|-------------------------------|
| FS | 19パケット×64B/フレーム ≈ 1.216 MB/s | **1.098 MB/s**（ESP32-S3、非同期キューdepth 2） |
| HS | 13トランザクション×512B/マイクロフレーム ≈ 53 MB/s | **36.4 MB/s**（ESP32-P4、非同期キューdepth 2、8KB転送） |

測定は [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/) です。理論値との差は、ホスト側のURB処理、転送間の隙間、デバイス側の受信能力から来ます。**設計では実測値を上限として見積もってください。** 例えばFSで320×240×16bppの画面を送るなら1フレーム153,600バイト、1.098MB/sなら約7fpsが上限です。

---

## 5. エンドポイント資源：チャネルとFIFO

エンドポイントの「claimに失敗する」には2つの独立した原因があります。**チャネル不足**と**FIFO不足**です。エラーコードは同じ `ESP_ERR_NOT_SUPPORTED` になることがあるので、区別が要ります。

### 5.1 チャネル会計

このライブラリの見積りは次の式です（`estimatedHcdChannelCount()`）。

```
EP0（デバイス1台につき1） + claimしたエンドポイント数 + ハブのエンドポイント数
```

`maxEndpointChannelCount()` は 8 を返します（ESP32-S3の `OTG_NUM_HOST_CHAN`）。診断用の見積りであり、HCDの実際の割り当てと完全に一致する保証はありませんが、枯渇の予測には十分使えます。

| API | 意味 |
|-----|------|
| `endpointChannelCount()` | claim済みインターフェースのエンドポイント数 |
| `managedEndpointCount()` | 常時受信転送を持つエンドポイント数 |
| `ep0ChannelCount()` | 追跡中のデバイス数（＝EP0の本数） |
| `hubEndpointChannelCount()` | ハブが持つエンドポイント数 |
| `estimatedHcdChannelCount()` | 上記の合計 |

枯渇時のログ:

```
No more HCD channels available
EP Alloc error: ESP_ERR_NOT_SUPPORTED
Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

対策は、台数を減らす、コンポジットデバイスの不要なインターフェースを持つ機器を避ける、`setHubTrackingEnabled(false)` でハブの追跡をやめる（ハブ情報APIは使えなくなりますが、配下のデバイスは動きます）、のいずれかです。

### 5.2 FIFO分割

ホストコントローラはハードウェアFIFOを3つに分け、その配分が**開けるエンドポイントの最大MPS**を決めます。単位は4バイトの「ライン」です。

| 対象 | 上限 |
|------|------|
| IN（種別問わず） | `(rxFifoLines - 2) * 4` |
| control / bulk OUT | `nptxFifoLines * 4` |
| interrupt / iso OUT | `ptxFifoLines * 4` |

HSポートの既定は rx = 総数 - 384、nptx = 256、ptx = 128 ラインで、**周期OUTが512バイトしかありません**。1024バイトのinterrupt OUTを持つ機器（HSのベンダーHIDパネルなど）はここで落ち、ドライバは次を出します。

```
HCD DWC: EP MPS (1024) exceeds supported limit (512)
```

対処は再分割です。

```cpp
EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
config.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // {260, 128, 280} lines
usb.begin(config);
```

制約:

- 総ラインは、P4のHSポートで1024ライン（4KB）、FSポートで256ライン（1KB）まで
- `rxFifoLines` と `nptxFifoLines` は0にできない（0にするとコントロール転送のFIFOが無くなる）
- 超過は `begin()` が `ESP_ERR_INVALID_SIZE` / `ESP_ERR_INVALID_ARG` で拒否する
- arduino-esp32 3.3.0以上が必要。それ未満は警告を出して既定値を使う

設定すると、起動ログに実際の上限が出ます。claim失敗と直接突き合わせてください。

```
FIFO lines rx=260 nptx=128 ptx=280 (total=668) -> max MPS in=1032 bulk_out=512 periodic_out=1120
```

FSポートは総量が256ラインしかないため、**そもそも1024バイトのエンドポイントは分割をどう変えても開けません。**

---

## 6. エラーとリカバリ

### 6.1 NAKとSTALLは別物

- **NAK** … 「今は用意がない」。ホストが再試行するので、エラーではありません。ポーリングでは常時発生します。
- **STALL** … 「その要求／転送は扱えない」。エンドポイントがhalt状態になり、`CLEAR_FEATURE(ENDPOINT_HALT)` で解除するまで通りません（EP0は次のsetupで自動復帰）。

### 6.2 ライブラリの復旧処理

IN転送がエラーで終わると、ライブラリは即座に再サブミットせず `recoveryPending` を立て、クライアントタスクの次の周回で `usb_host_endpoint_clear()`（＝halt解除）を行ってから再サブミットします。これは**エンドポイントのコールバックが `DEV_GONE` イベントより先に届く**ため、切断済みのデバイスへ新しいURBを投げないようにするための順序制御です。

したがって、抜線直後にエラーログが1〜2行出てから切断が処理されるのは正常な流れです。

bulk OUT側は挙動が違います。転送エラーでパイプがhaltすると、ESP-IDFはhaltが解除されるまで以後のsubmitをすべて拒否します。ライブラリは**次の書き込みのときに、呼び出し元タスク上で `usb_host_endpoint_clear()` を実行して自動復帰**します。halt解除はブロックしうる処理なので、完了コールバックではなく呼び出し側で行う設計です。

ここに例外があります。**USBクライアントタスク（＝コールバックの中）から書いた場合は復帰できません。**

```
vendor bulk OUT halted; cannot recover from the USB client task
```

このとき戻り値は `false`、`lastError()` は `ESP_ERR_INVALID_STATE` です。コールバックから送信していると、1度のエラーで送信経路が固まったままになります（[8章](#8-コールバックのコンテキスト)の理由がもう1つ増えるわけです）。手動で解除するなら、コンソールから次を送ります。

```
ctl 02 01 0000 0001 0      # CLEAR_FEATURE(ENDPOINT_HALT) を EP 0x01 に
```

### 6.3 転送の終端

バルク転送は、**MPS未満のパケット（short packet）**が来た時点で終わります。転送長がMPSの倍数ちょうどのとき、受け手は「まだ続く」と解釈するため、プロトコルによっては**ZLP（長さ0パケット）**が必要です。

- 都度送る: `vendorWriteZlp()`
- 自動化: `vendorSetAutoZlp(true)`（非同期キュー使用時はスロットを1つ余分に使うので depth ≥ 2）

ADBやCDC-NCMがこれを要求します。「大きいデータだけ止まる」「特定サイズでハングする」ときは、まずこれを疑ってください。

### 6.4 タイムアウト

同期API（`vendorControlTransfer()`, `vendorReadSync()`, MSC、CCID）は既定タイムアウトを持ちます（コントロール1000ms、MSC 5000ms、CCID 5000ms）。タイムアウトは `lastError()` に `ESP_ERR_TIMEOUT` を残します。値そのものより、**なぜ応答が来ないか**（モード違い、要求の未サポート、halt）を先に疑ってください。

---

## 7. スループット設計

### 7.1 同期書き込みの限界

`vendorWrite()` / `sendSerial()` は1転送ごとに完了を待ちます。待っている間バスは空くので、転送が小さいほど効率が落ちます。実測でも、小さい転送ほど非同期キューとの差が開きます。

### 7.2 非同期キュー

```cpp
usb.vendorWriteQueueBegin(2 /* depth */, 8192 /* bytes per slot */);

size_t capacity = 0;
uint8_t *buffer = usb.vendorWriteAcquire(&capacity, 100 /* ms */);
if (buffer) {
  size_t n = encodeInto(buffer, capacity);   // ゼロコピー：DMAバッファに直接書く
  usb.vendorWriteSubmit(buffer, n);
}
```

要点:

- **depthは2で足りることが多い**。実測のピークはFS/HSともdepth 2で出ています。深くするほどDMAメモリを食います（上限8）
- `vendorWriteAcquire()` → `vendorWriteSubmit()` はゼロコピー。`vendorWriteAsync()` はコピーする簡易版
- キューは**バックプレッシャ**になります。空きスロットがなければ `vendorWriteAcquire()` が待つので、送り手が暴走してDMAメモリを食い潰すことがありません
- `vendorWriteStats()` で `submitted / completed / errors / queueFullEvents / zlp / bytes` を確認できます。`queueFullEvents` が多ければ送り手がバスより速い＝上限に達しています
- 非同期APIは完了を待たないので、**USBコールバックからでも呼べます**

CDCシリアル側にも同形のキューがあります（`serialWriteQueueBegin()`）。こちらは、使わないと `sendSerial()` が転送を無制限に積み上げてDMAメモリを枯渇させうる、という理由でも有用です。

### 7.3 受信側

- vendor bulk INの受信バッファは既定512バイト（`ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE`、ビルドフラグで変更）
- CDCの受信リングは既定512バイト。**実行時に `setRxBufferSize()`** で変えられる（推奨）
- 溢れた分は古いバイトから捨てられ、エラーは出ません。高ボーレートで取りこぼすならここを増やします

### 7.4 メモリとキャッシュ

転送バッファはDMA可能メモリから確保されます（`usb_host_transfer_alloc`）。ESP32-P4ではこの領域がキャッシュされるため、IN転送の直前にライブラリが `esp_cache_msync()` でキャッシュラインを書き戻します。アプリ側の対処は不要です（[`msc_cache_coherency`](../tests/manual/msc_cache_coherency/) で検証）。

---

## 8. コールバックのコンテキスト

**すべてのコールバックはUSBクライアントタスクで走ります。** `loop()` とは別タスクです。ここでの制約は明確です。

| してよいこと | してはいけないこと |
|-------------|------------------|
| データをコピーする、フラグを立てる、キューに積む | 完了待ちする同期API（`vendorWrite`, `vendorReadSync`, `vendorControlTransfer`, MSC, CCID, `end()`） |
| 非同期キューへの `vendorWriteSubmit()` | `delay()` や長いループ |
| 短いログ出力 | 大きなヒープ確保、ファイルI/O、ネットワーク処理 |

同期APIは、クライアントタスクから呼ばれると自分で検知して `false` を返します（自分の完了イベントを自分で待つデッドロックを避けるため）。**「コールバックで送信したら動かない」の大半はこれです。** 正しい形は次のとおりです。

```cpp
volatile bool sendRequested = false;

usb.onVendorData([](const EspUsbHostVendorData &data) {
  // data.data はこのコールバックの間だけ有効。必要ならコピーする
  memcpy(rxBuffer, data.data, min(data.length, sizeof(rxBuffer)));
  sendRequested = true;          // 送信は loop() でやる
});

void loop() {
  if (sendRequested) {
    sendRequested = false;
    usb.vendorWrite(payload, sizeof(payload));
  }
}
```

**データポインタの寿命**も共通の注意点です。`onVendorData` / `onHIDInput` / `onNetworkFrame` などが渡すポインタは、ライブラリ内部のバッファを指しており、コールバックから戻ると再利用されます。後で使うなら必ずコピーしてください。

リスナ（`addKeyboardListener()` など）は、`on*()` の後に登録順で呼ばれます。イベントごとにコールバック集合のスナップショットが取られるので、**コールバックの中で追加・削除しても、効くのは次のイベントから**です。既定は1イベントあたり4個（`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`）。

---

## 9. 新しいクラス／プロトコルを実装する

未対応クラスは、ほぼすべて「汎用APIでインターフェースを開き、中身を自分で喋る」で実装できます。このリポジトリのプリンタ、USBTMC、ADB、DisplayLink、AX206、DP100はすべてこの形です。

### 9.1 インターフェースを開く

```cpp
// クラスを問わず、番号でインターフェースを指定できる
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
```

`vendorOpen()` は既定では最初の `0xff` インターフェースを選びますが、**番号を明示すればクラスに関係なくclaimします**。プリンタ（0x07）やUSBTMC（0xfe）がこの形で動きます。ライブラリ自身が既にclaimしているインターフェース（HID、CDC、MSCなど）は拒否されます。

開いた後の情報:

```cpp
usb.vendorOutEndpoint(address);   // 選ばれたbulk OUTのアドレス
usb.vendorInEndpoint(address);
usb.vendorOutPacketSize(address); // MPS。ZLP判定と分割に必要
```

インターフェースに複数のbulkエンドポイントがある場合、どれが選ばれたかは必ず確認してください。

### 9.2 クラス要求はEP0で送る

多くのクラスは、bulkの前後にEP0のクラス要求を使います。

```cpp
// USBTMC GET_CAPABILITIES（クラス・インターフェース宛・IN）
usb.vendorControlTransfer(0xa1, 7, 0, interfaceNumber, buf, sizeof(buf), &actual, address);

// プリンタ SOFT_RESET（クラス・インターフェース宛・OUT）
usb.vendorControlTransfer(0x21, 2, 0, interfaceNumber, nullptr, 0, nullptr, address);
```

`wIndex` にインターフェース番号を入れるのを忘れると、多くのデバイスがSTALLします。

### 9.3 実装の順序

1. **読み出し系だけで往復を確立する**（識別要求、状態取得）。ここが通れば、宛先とエンドポイントの選択は正しい
2. **初期化シーケンスを完全に再現する**。キャプチャにあって省いた要求がないか
3. **状態機械にする**。要求→応答の対応、タイムアウト、リトライを明示的に持つ
4. **書き込み系を足す**。物理的影響のあるコマンドは最後
5. **エラー経路を試す**（抜線、halt、タイムアウト）。実運用で最初に壊れるのはここ

### 9.4 参考にする実装

| やりたいこと | 参考 |
|-------------|------|
| bulk往復の最小形 | [`EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) |
| クラス要求＋bulkメッセージ層 | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/)（class 0xfe） |
| クラス要求＋状態ポーリング＋大きな一括送信 | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/)（class 0x07） |
| HIDに載った独自フレーム＋CRC | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) |
| 認証つきの多重ストリーム | [`EspUsbHostAdbConnect`](../examples/Vendor/EspUsbHostAdbConnect/) |
| 高スループットの非同期キュー活用 | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) |
| セッション維持（キープアライブ）が要る機器 | [`EspUsbHostMacroPadN3`](../examples/HID/EspUsbHostMacroPadN3/) |

---

## 10. 計測とデバッグ

### 10.1 ログを読む

Core Debug Level を `Verbose` にすると、ESP-IDFのホストスタックが列挙とチャネル割り当ての詳細を出します。注目する行:

| ログ | 意味 |
|------|------|
| `No more HCD channels available` | チャネル枯渇（[5.1](#51-チャネル会計)） |
| `EP MPS (n) exceeds supported limit (m)` | FIFO分割不足（[5.2](#52-fifo分割)） |
| `Enqueue URB error: ESP_ERR_INVALID_STATE` | 切断直後の転送。抜線時に1〜2行なら正常 |
| `device_release ... ext_hub.c` のassert | IDFハブドライバのクラッシュ。既知の組み合わせを確認 |
| `FIFO lines rx=... -> max MPS ...` | 自分で設定したFIFO分割の実効値 |

### 10.2 何を測るか

| 知りたいこと | 手段 |
|-------------|------|
| 構成・クラス・エンドポイント | [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) |
| 生ディスクリプタとの突き合わせ | [`raw_descriptor`](../tests/manual/raw_descriptor/) と PC側の `lsusb -v` |
| 任意の転送を試す | [`EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/) |
| チャネル使用量 | `estimatedHcdChannelCount()` / `printAllDeviceInfo()` |
| バルクの実効速度 | [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/) |
| 抜き挿し耐性 | [`hotplug`](../tests/manual/hotplug/) |
| ハブが原因かの切り分け | [`tests/probe/hub_enum`](../tests/probe/) |
| P4のポート特定 | [`tests/probe/`](../tests/probe/) の `p4_hs_host` / `p4_fs_host` / `p4_cdc` |

`tests/probe/` は正式な回帰テストではなく、**ブリングアップとプロトコル解析のための使い捨てスケッチ置き場**です。同じ用途の新しい調査を始めるときは、ここに追加するのが既存の作法です。

### 10.3 切り分けの原則

1. **PC側で同じことを試す。** PCで動かないならESP32の問題ではありません
2. **直結とハブ経由を比べる。** 差が出たらチャネルか電源かハブ固有の問題です
3. **他のデバイスに替える。** キーボードで動けばホスト側は正常です
4. **速度を変える（P4）。** FSで駄目、HSで動く（あるいは逆）なら、MPSかハブか帯域の問題です
5. **1つずつ戻す。** FIFO設定、キュー深さ、タスク優先度は同時に変えないこと

---

## 関連ドキュメント

- [USB Host開発ガイド（入門編）](usb-host-guide.ja.md) — 基礎、電源、実験手順、キャプチャ
- [動作確認済みデバイスとボード](tested-devices.ja.md) — 実機で確認した機器と条件
- [README.ja.md](../README.ja.md) — APIリファレンスとクラス対応状況
- [tests/manual/README.ja.md](../tests/manual/README.ja.md) — マニュアルテスト一覧と既知の問題
- [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) — テスト戦略
- 個別プロトコルの解析メモ: [printer-spec.ja.md](printer-spec.ja.md) / [usbtmc-spec.ja.md](usbtmc-spec.ja.md) / [ccid-api-spec.ja.md](ccid-api-spec.ja.md) / [dp100-spec.ja.md](dp100-spec.ja.md) / [usb-display-spec.ja.md](usb-display-spec.ja.md) / [usb-network-spec.ja.md](usb-network-spec.ja.md) / [vendor-api-spec.ja.md](vendor-api-spec.ja.md)
