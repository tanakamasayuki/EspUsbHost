# USB Host開発ガイド

> English: [usb-host-guide.md](usb-host-guide.md)

USBデバイスをESP32につないで動かすまでの手引きです。前半はUSB Hostそのものの基礎、後半はESP32シリーズ固有の注意点と、実際に手を動かす実験手順です。

より踏み込んだ内容（ディスクリプタのバイト構造、チャネルとFIFO、エラー復帰、スループット設計、コールバックのコンテキスト、独自クラスの実装）は [上級編](usb-host-advanced.ja.md) にあります。

USBを触るのが初めてでも読めるように書いていますが、「なぜ動かないのか」を自分で切り分けられるようになることを目標にしているので、原理の説明は省いていません。急ぐ場合は [実験の進め方](#4-実験の進め方) から読み、わからない用語が出てきたら前半に戻ってください。

## 目次

1. [USB Hostの基礎](#1-usb-hostの基礎)
2. [電源とハブ](#2-電源とハブ)
3. [ESP32シリーズ固有の注意点](#3-esp32シリーズ固有の注意点)
4. [実験の進め方](#4-実験の進め方)
5. [プロトコルの解析](#5-プロトコルの解析)
6. [トラブルシューティング](#6-トラブルシューティング)
7. [ツール一覧](#7-ツール一覧)
8. [参考資料](#8-参考資料)

---

## 1. USB Hostの基礎

### 1.1 ホストとデバイス

USBは対等な通信ではありません。**ホストが1台、デバイスがぶら下がる**構造で、通信は必ずホストが起点になります。デバイスが自発的にデータを送りつけることはできず、ホストが「何かある?」と聞きに行って初めてデータが返ります。

PCがホスト、キーボードやUSBメモリがデバイスです。このライブラリを使うとき、ESP32はPCの側、つまり**ホスト**になります。

ここで最初に混乱しやすいのが、ESP32ボードのUSBコネクタです。同じUSB Type-Cコネクタでも、

- **PCにつなぐためのコネクタ**（ESP32がデバイス側。書き込みやシリアルモニタに使う）
- **USB機器をつなぐためのコネクタ**（ESP32がホスト側。このライブラリが使う）

は役割がまったく違います。ボードによってどちらがどちらかは異なり、シルク印刷の`USB`/`OTG`/`UART`という表記も当てになりません。回路図で確認してください。

### 1.2 4本の線

標準的なUSB 2.0のケーブルには4本の線があります。

| 線 | 役割 |
|----|------|
| VBUS | 電源 +5V。**ホストからデバイスへ供給する** |
| D+ / D- | データ2本（差動信号） |
| GND | グラウンド |

ホストになるということは、**電源を供給する側になる**ということでもあります。ここが組み込みボードで最初につまずく点です。ボードのUSBコネクタに5Vが出ていなければ、配線がいくら正しくてもデバイスは起動しません（[2章](#2-電源とハブ)）。

「充電専用ケーブル」にはD+/D-が入っていません。デバイスがまったく認識されないとき、まずケーブルを疑ってください。

### 1.3 速度

| 速度 | 略称 | 転送レート | 主な用途 |
|------|------|-----------|---------|
| Low Speed | LS | 1.5 Mbps | 古いマウス・キーボード |
| Full Speed | FS | 12 Mbps | キーボード、マウス、シリアル変換、MIDI、多くの小型機器 |
| High Speed | HS | 480 Mbps | USBメモリ、カメラ、オーディオIF、Ethernetアダプタ |
| SuperSpeed | SS | 5 Gbps以上 | USB 3.x機器。**ESP32では扱えません** |

USB 3.x機器の多くはUSB 2.0にフォールバックして動きますが、これは機器次第です。SS専用の機器はESP32では使えません。

**速度は機器が決めるものであり、ホストが選べるものではありません。** ただしホスト側が対応していない速度の機器は動きません。ESP32-S2/S3のホストはFSまで、ESP32-P4はFSポートとHSポートの両方を持ちます（[3章](#3-esp32シリーズ固有の注意点)）。

### 1.4 列挙（enumeration）

デバイスを挿してからアプリケーションが使えるようになるまでに、ホストは次の手順を踏みます。この流れを知っていると、ログのどこで失敗したかが読めるようになります。

1. **接続検出** — D+/D-のプルアップで、デバイスが挿さったことと速度をホストが検出する
2. **リセット** — バスをリセットする
3. **アドレス割り当て** — デバイスに1〜127のアドレスを割り当てる
4. **デバイスディスクリプタ取得** — VID/PID、USBバージョン、クラスなどを読む
5. **コンフィグレーションディスクリプタ取得** — インターフェースとエンドポイントの構成をまとめて読む
6. **コンフィグレーション選択** — 通常は最初の1つを有効にする
7. **インターフェースのclaim** — ドライバ（このライブラリ）が担当するインターフェースを確保し、エンドポイントを開く

`usb.onDeviceConnected()` が呼ばれるのは、7が終わったあとです。ここに到達しない場合、問題は電気的なもの（1〜2）か、ディスクリプタが読めない/大きすぎる（4〜5）か、リソース不足（7）のいずれかです。

### 1.5 ディスクリプタの階層

デバイスは自分の構成を**ディスクリプタ**という構造体の列で申告します。階層はこうなっています。

```
Device（デバイス全体：VID/PID、EP0のサイズ）
└── Configuration（消費電流、バスパワー/セルフパワー）
    ├── Interface 0（クラス／サブクラス／プロトコル ← 「何であるか」はここ）
    │   ├── クラス固有ディスクリプタ（HIDディスクリプタ、CDC機能ディスクリプタ など）
    │   ├── Endpoint 0x81（IN, interrupt, 最大パケット8, interval 10）
    │   └── Endpoint 0x01（OUT, ...）
    └── Interface 1
        └── ...
```

重要な点が3つあります。

- **クラスはデバイスではなくインターフェースに付く。** デバイスディスクリプタのクラスは `0x00`（インターフェースごとに決まる）であることが多く、実体はインターフェースを見ないとわかりません。
- **1つのデバイスが複数のインターフェースを持てる（コンポジットデバイス）。** キーボード兼マウス、キーボード兼ベンダー独自機能、といった構成は普通にあります。IAD（Interface Association Descriptor, `bDescriptorType=0x0b`）で複数インターフェースが1つの機能としてまとめられていることもあります。
- **クラス固有ディスクリプタは標準の解析では出てこない。** HIDのレポートディスクリプタ参照、CDCの機能ディスクリプタ、CCIDのクラスディスクリプタなどは、コンフィグレーションディスクリプタの生バイト列を自分で辿る必要があります。[`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) がこれを行います。

### 1.6 エンドポイントと転送タイプ

エンドポイントは通信の口です。アドレス（`0x81` のようにbit7が方向、下位4bitが番号）、方向（IN=デバイス→ホスト、OUT=ホスト→デバイス）、転送タイプ、最大パケットサイズ（MPS）、ポーリング間隔（interval）を持ちます。

| 転送タイプ | 特徴 | 使われる例 |
|-----------|------|-----------|
| **Control** | EP0で行う設定用の転送。要求／応答型。全デバイスが必ず持つ | 列挙、クラス要求、HIDのSET_REPORT |
| **Interrupt** | 一定間隔でホストがポーリングする。少量・低遅延 | キーボード、マウス、CCIDの状態通知 |
| **Bulk** | 大量データ。帯域保証なし、エラー再送あり | USBメモリ、プリンタ、シリアル変換、ベンダー独自 |
| **Isochronous** | 一定帯域を予約。エラー再送なし | オーディオ、カメラ |

方向は**ホストから見た向き**です。「IN」はデバイスからホストにデータが来ます。

エンドポイントはホスト側のハードウェア資源（チャネル）を消費します。ここがESP32では効いてきます（[3.4](#34-チャネル数の制限)）。

### 1.7 クラスと「見え方」

USBには標準クラスがあり、クラスコードが同じなら同じ手順で扱えます。主なものは次のとおりです。

| クラス | コード | このライブラリでの扱い |
|--------|--------|----------------------|
| Audio / MIDI | `0x01` | ライブラリAPI（[Audio](../examples/Audio/) / [MIDI](../examples/MIDI/)） |
| CDC（シリアル、Ethernet） | `0x02` / `0x0a` | ライブラリAPI（[Serial](../examples/Serial/) / [UsbNetwork](../examples/UsbNetwork/)） |
| HID | `0x03` | ライブラリAPI（[HID](../examples/HID/)） |
| Printer | `0x07` | サンプル（[EspUsbHostPrinterEscPos](../examples/Vendor/EspUsbHostPrinterEscPos/)） |
| Mass Storage | `0x08` | ライブラリAPI（[Storage](../examples/Storage/)） |
| Hub | `0x09` | ライブラリAPI |
| Smart Card (CCID) | `0x0b` | ライブラリAPI（[Ccid](../examples/Ccid/)） |
| Video (UVC) | `0x0e` | **非対応**（[3.5](#35-コントロール転送256バイトの壁)） |
| Application Specific (USBTMC, DFU) | `0xfe` | サンプル（[EspUsbHostUsbtmcScpi](../examples/Vendor/EspUsbHostUsbtmcScpi/)） |
| Vendor Specific | `0xff` | ライブラリAPI（[Vendor](../examples/Vendor/)） |

ここで初心者が必ずはまるのが、**「同じ用途の機器でも、USB上の見え方は機器ごとに違う」**という事実です。

| 機器 | ありがちな見え方 |
|------|----------------|
| バーコードスキャナ | HIDキーボード（読取結果がキー入力として飛ぶ）／CDCシリアル／HIDのベンダー独自レポート。**機種や設定バーコードで切り替わる** |
| レシートプリンタ | Printerクラス `0x07` ／ CDCシリアル `0x02` ／ ベンダー独自 `0xff`。ディップスイッチや設定ツールで切り替わる機種がある |
| USBシリアル変換 | CDC-ACM `0x02` ／ ベンダー独自 `0xff`（FTDI、CP210x、CH34x はこちら） |
| 計測器 | USBTMC `0xfe/0x03` ／ CDCシリアル ／ ベンダー独自 |
| ゲームパッド | HID ／ ベンダー独自（XInput系） |
| 電源・測定モジュール | HIDのレポートに独自プロトコルを載せている（例: [DP100](../examples/HID/EspUsbHostDp100Power/)） |

つまり、**「バーコードスキャナ対応」という機能は存在せず、あるのは「そのバーコードスキャナがどう見えるかへの対応」だけです。** 手元の個体が何に見えるかを最初に確認する、という手順（[4章](#4-実験の進め方)）が必要なのはこのためです。

「キーボードに見える」機器はキーボードとして、そのまま `onKeyboard()` で動きます。これは対応が最も簡単なパターンなので、設定で切り替えられる機器なら、まずHIDキーボードモードやCDCモードにできないか確認する価値があります。

---

## 2. 電源とハブ

USB Hostのトラブルの多くはソフトではなく電源です。

### 2.1 ボードがVBUSを出しているか

**最初に確認すべき点です。** ホスト用コネクタのVBUSに5Vが出ていないボードがあります。

- Espressif公式の **ESP32-S3-DevKitC-1** は、USB OTGコネクタから接続デバイスへ電源を供給しません。デバイス側に別途電源を用意するか、セルフパワーハブを挟む必要があります。
- M5Stack製品の一部は、USBコネクタの電源をソフトウェアで制御できます。製品ごとの回路図と手順を確認してください。
- **Freenove ESP32-S3-WROOM Board** のように、OTG Type-Cコネクタから給電できるボードもあります。素直に始めたい場合はこの種のボードを推奨します。

テスターでコネクタのVBUS-GND間を測るのが確実です。0Vなら、そのコネクタでは何をしてもデバイスは動きません。

開発中は**USBコネクタが2つあるボード**を推奨します。書き込み・シリアルモニタ用（USB-UART）とホスト用（OTG）を分けられるからです。最終製品はAtomS3のようなコネクタ1つの製品でも構いません。

### 2.2 電流が足りているか

コンフィグレーションディスクリプタの `bMaxPower` は、そのデバイスがバスから取ると申告している電流です（単位は2mA）。ただし**申告と実際の消費は別物**で、動作中のピークはもっと大きいことがあります。

電流不足で起きる典型的な症状は次のとおりです。

- 挿しても列挙されない
- 列挙はするが、書き込み・印字・モータ動作など負荷がかかった瞬間に切断される
- ESP32側がブラウンアウトリセットする（`Brownout detector was triggered`）

対策は、**セルフパワー（AC電源付き）のUSBハブ**を挟むことです。バスパワーのハブはESP32側の電源をそのまま分けるだけなので、電力不足の解決にはなりません。2.5インチHDD、プリンタ、無線アダプタ、USBディスプレイなどは特に注意が必要です。

### 2.3 ハブ

複数のデバイスを同時に使うにはハブが要ります。実務上の注意点です。

- **セルフパワーのUSB 2.0ハブ**を使ってください。USB 3.xハブや、内部でハブが多段になっている製品は挙動が異なり、検証も十分ではありません。
- ハブ自体もチャネルとアドレスを消費します（[3.4](#34-チャネル数の制限)）。
- ハブとデバイスの組み合わせによっては、ESP-IDF側のハブドライバが落ちる既知の例があります。[tests/manual/README.ja.md](../tests/manual/README.ja.md) の「ESP-IDFがクラッシュするハブ組み合わせ」を参照してください。
- ポート単位で電源を制御できるハブ（PPPS対応）なら、[`EspUsbHostHubPPPS`](../examples/Info/EspUsbHostHubPPPS/) からデバイスの電源を落として再列挙させられます。デバッグに有用です。

---

## 3. ESP32シリーズ固有の注意点

### 3.1 対応チップとポート

| チップ | ホスト能力 |
|--------|-----------|
| ESP32-S2 | FSのホスト1つ。内蔵RAMが少なく `ESP_USB_HOST_MAX_DEVICES` の既定は3 |
| ESP32-S3 | FSのホスト1つ。このライブラリの主対象 |
| ESP32-P4 | FS OTGとHS OTGの2つ。ただし**同時にホストとして使えるのは1つだけ** |
| ESP32（無印）, C3, C6 など | USB OTGを持たない、またはホストに使えない。**このライブラリは動きません** |

ESP32-P4では `EspUsbHostConfig::port` で `ESP_USB_HOST_PORT_FULL_SPEED` / `ESP_USB_HOST_PORT_HIGH_SPEED` を選びます。ESP-IDFのホストスタックは同時に1つのホストペリフェラルしか扱えないため、FSとHSを両方ホストとして動かすことはできません。

P4のFSポートはピン配置が固定ではなく、USB Serial/JTAGとPHYを共有しています。詳細は [README.ja.md のESP32-P4に関する注意](../README.ja.md) と [`EspUsbHostP4FsPhyRouting`](../examples/Info/EspUsbHostP4FsPhyRouting/) を参照してください。

### 3.2 FSポートとHSポートの使い分け（P4）

これは実際に何を接続できるかを左右する、最も重要な選択です。

**FSポートを選んだ場合**

- 接続されるデバイスはすべてFS（または LS）として動作します。
- ハブを介した複数デバイスの同時利用ができます。キーボード＋マウス＋シリアル、といった構成はこちらです。
- **FSでは動かない機器があります。** HS専用の機器、あるいはFS構成を持たない機器（HS前提の最大パケットサイズ1024バイトのエンドポイントを持つ機器、UAC2でFS用のコンフィグを持たないオーディオ機器など）はここでは使えません。実例として、[Mirabox N3系マクロパッド](../examples/HID/EspUsbHostMacroPadN3/) はinterrupt OUTのMPSが1024バイトのため、P4のHSポートでしか動きません。

**HSポートを選んだ場合**

- HS機器を本来の速度で使えます。バルク転送のスループットは実測でFSの約1.1MB/sに対し約36MB/s（[`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/)）。
- **ハブは実質的に使えません。** USB 2.0の規格上は、HSハブがTransaction Translatorを持つことでHSポートの下にFS/LS機器をぶら下げられますが、現状のESP-IDF環境ではこの経路が使えず、HSポートでのハブ利用は事実上できません。
- 結果として、**HSのデバイスとFSのキーボードを同時に使う、といった構成は取れません。** HSポートには機器を1台直結する、と考えてください。

つまり選択は「速度を取るか、同時接続を取るか」です。迷ったら、まずFSポートで動くかを確認し、FSで動かない機器・帯域が足りない機器だけHSポートに回してください。

ESP32-S2/S3にはHSポートがないため、この選択自体が存在しません。S3で動かない高速機器は、P4のHSポートでなら動く可能性があります。

### 3.3 Arduinoのビルド設定に由来する制限

このライブラリはArduino-ESP32が配布する**ビルド済み**のESP-IDF USB Hostスタックの上で動きます。そのビルド時の設定（sdkconfig）はスケッチ側からもライブラリ側からも変更できません。「Arduino環境だから使えない」機能があるのはこのためです。

代表例が次のコントロール転送サイズの制限で、USBカメラが使えない直接の原因です。

### 3.4 チャネル数の制限

ESP32-S3のUSBホストは**8チャネル**しか持ちません（`OTG_NUM_HOST_CHAN`）。EP0、claimした各エンドポイント、ハブがそれぞれ消費します。

複合デバイス、ハブ、オーディオ、MSC、複数のシリアルを組み合わせると、すぐに枯渇します。実測例として、ハブ経由でFTDI+CP210xは動くがFTDI+CH34xはチャネル不足で失敗する、という組み合わせが記録されています（[tests/manual/README.ja.md](../tests/manual/README.ja.md)）。

ログに次のいずれかが出たら、チャネル不足です。

```
No more HCD channels available
EP Alloc error: ESP_ERR_NOT_SUPPORTED
Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

`usb.endpointChannelCount()` / `usb.maxEndpointChannelCount()` / `usb.estimatedHcdChannelCount()` で使用状況を確認できます。[`EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) と [`EspUsbHostDeviceInfo`](../examples/Info/EspUsbHostDeviceInfo/) が表示します。

### 3.5 コントロール転送256バイトの壁

Arduino-ESP32のビルド済みホストスタックは `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` でビルドされています。1回のコントロール転送は、8バイトのsetupパケットを含めて256バイトまでです。ここから2つの帰結があります。

1. **コンフィグレーションディスクリプタ全体が256バイトを超えるデバイスは列挙に失敗します。** クラスドライバが動く以前の問題です。USBカメラ（UVC）が使えないのはこれが理由で、たとえばLogitech C920のコンフィグレーションディスクリプタは1,974バイトあります。これはスケッチやライブラリのオプションでは回避できません。Arduino-ESP32側がこの値を上げれば状況は変わります。
2. **1回のGET_DESCRIPTORで読めるのは248バイトまで**です。ディスクリプタの生バイトを読むツールはこの範囲で打ち切られます。

### 3.6 ESP32-S2のメモリ

ESP32-S2は内蔵RAMが少ないため、`ESP_USB_HOST_MAX_DEVICES` の既定値が3です（S3は8）。増やすには `-DESP_USB_HOST_MAX_DEVICES=N` を指定しますが、`dram0_0_seg overflowed` に注意してください。大きめのサンプル（`UsbNetwork` など）はS2に収まりません。

### 3.7 その他

- **P4のキャッシュ整合性** — P4ではUSB転送用のDMAメモリがキャッシュされます。ライブラリ側がIN転送のたびにキャッシュを書き戻すため、アプリ側の対処は不要です（[`msc_cache_coherency`](../tests/manual/msc_cache_coherency/) で検証）。
- **コアのバージョン** — ESP32-S2/S3はarduino-esp32 3.2.0以上、ESP32-P4は3.3.1以上が必要です。
- **ログレベル** — 列挙に失敗する原因はESP-IDF側のログにしか出ないことが多いため、調査時はCore Debug Levelを`Verbose`にしてください。

---

## 4. 実験の進め方

未知のデバイスを動かすまでの手順です。**上から順に、飛ばさずに**進めてください。多くの失敗は、後段の工程で前段の問題（電源、コネクタ）を悩んでいるために起きます。

### Step 0. 机の上の確認（ソフトを書く前）

- ホスト用コネクタのVBUSに5Vが出ているか、テスターで測る
- 使うケーブルがデータ線入りか（充電専用でないか）確認する
- デバイスをPCに挿して、PC側では正常に動くことを確認しておく

### Step 1. USB Hostが起動し、デバイスが列挙されるか

[`examples/Info/EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) を書き込みます。

- `usb.begin(): ok` が出るか → 出なければビルド／ターゲット／コアバージョンの問題
- デバイスを挿して `ENUMERATED` が出るか → 出なければ電源・コネクタ・ケーブル・機器側の問題（このスケッチがチェックリストを表示します）
- 速度（FS/HS/LS）とVID:PIDを控える

まずは普通のUSBキーボードかUSBメモリで試してください。それが列挙できればボードは正常で、問題は対象デバイス固有だと切り分けられます。

### Step 2. そのデバイスが何であるかを調べる

[`examples/Info/EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) を書き込みます。次が表示されます。

- 解析済みのデバイス／インターフェース／エンドポイント情報
- インターフェースごとの「これは何で、どのAPI・どのサンプルで扱えるか」
- デバイス／コンフィグレーションディスクリプタの**生バイト**と、ブロック単位の走査

ここで確認するのは次の点です。

- インターフェースはいくつあり、それぞれのクラス／サブクラス／プロトコルは何か
- エンドポイントの種類（interrupt / bulk / isochronous）、方向、MPS
- ライブラリが `claimed=yes` にしたインターフェースはどれか
- 見慣れないクラス固有ディスクリプタ（`CS_INTERFACE` など）があるか

同じことを開発者向けに行うのが [`tests/manual/device_dump`](../tests/manual/device_dump/) と [`tests/manual/raw_descriptor`](../tests/manual/raw_descriptor/) です。pytestから実行してログを残せます。

```sh
cd tests
uv run --env-file .env pytest manual/device_dump/device_dump.py -v -s
uv run --env-file .env pytest manual/raw_descriptor/raw_descriptor.py -v -s
```

参考として、PC側で `lsusb -v -d <vid>:<pid>`（Linux）やUSB Tree Viewer（Windows）の出力も取っておくと、後の比較が楽になります。

### Step 3. 対応するクラスAPIで動かしてみる

Step 2でクラスがわかったら、対応するサンプルをそのまま書き込みます（[1.7の表](#17-クラスと見え方)）。多くのデバイスはここで動きます。

- HID → [`EspUsbHostHIDRawDump`](../examples/HID/EspUsbHostHIDRawDump/) で生レポートを見る。キーボード／マウスなら専用サンプル
- CDC / VCP → [`EspUsbHostUSBSerial`](../examples/Serial/EspUsbHostUSBSerial/)
- MSC → [`EspUsbHostMSCFatList`](../examples/Storage/EspUsbHostMSCFatList/)
- CCID → [`EspUsbHostCcidReader`](../examples/Ccid/EspUsbHostCcidReader/)
- MIDI / Audio → [MIDI](../examples/MIDI/) / [Audio](../examples/Audio/)

### Step 4. HIDなら、レポートディスクリプタを読む

HIDデバイスは、データの意味を**レポートディスクリプタ**として自分で申告します。[`EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/) で取得・簡易デコードできます。

ゲームパッドや独自機器では、ここに書かれたUsage Page / Usage / Report Size / Report Countから、入力レポートのどのビットが何なのかを組み立てます。ベンダー独自Usage（`0xff00`以降）のレポートは、中身が独自プロトコルであることを意味します。DP100やマクロパッドのように、**HIDの皮をかぶった独自プロトコル**は珍しくありません。この場合はStep 5に進みます。

### Step 5. 公開情報を探す

独自プロトコルだとわかったら、**自分で解析する前に**次を探してください。多くの機器はすでに誰かが解析しています。

- メーカーの公開するプロトコル仕様書・SDK・APIドキュメント
- Linuxカーネルのドライバ（`drivers/usb/`, `drivers/hid/`, `drivers/net/usb/` など）。動作するコードは事実上の仕様書です
- libusbベースのOSSプロジェクト、Python実装、`sigrok` のプロトコルデコーダ
- 同種の機器を扱う他のマイコン向けライブラリ
- ESC/POS、SCPI、CCID、ADBのような**業界標準プロトコル**。クラスは`0xff`でも中身は標準ということがあります

このライブラリのサンプル群自体もこの形で書かれています。プリンタ（ESC/POS）、計測器（USBTMC + SCPI）、Android（ADB）、DisplayLink、AX206などは、いずれも「クラスは汎用APIで開き、中身は公開されたプロトコルで喋る」という構造です。仕様のメモは [`docs/`](.) 以下に残しています（[printer-spec.ja.md](printer-spec.ja.md)、[usbtmc-spec.ja.md](usbtmc-spec.ja.md)、[ccid-api-spec.ja.md](ccid-api-spec.ja.md)、[dp100-spec.ja.md](dp100-spec.ja.md) など）。

### Step 6. 情報がなければ、PCでキャプチャして解析する

公開情報がない、または不十分な場合は、**PC上で純正ソフトが機器と喋っている内容をキャプチャ**して読み解きます。手順は[5章](#5-プロトコルの解析)にまとめます。

### Step 7. ESP32上で再現する

キャプチャから読み取った転送を、[`examples/Vendor/EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/) でそのまま打ち込みます。ビルドし直さずに1転送ずつ試せるので、解析のサイクルが速くなります。

```
> ctl 80 06 0100 0000 12        # まずGET_DESCRIPTOR(DEVICE)で疎通確認
> open 0                        # インターフェース0をclaim
> out 10 04 01                  # キャプチャで見たコマンドを送る
> in 40                         # 応答を読む
```

応答がキャプチャと一致したら、そのやり取りは理解できたことになります。失敗も情報です（[コンソールのREADME](../examples/Vendor/EspUsbHostProtocolConsole/README.ja.md#失敗の読み方)）。

### Step 8. スケッチに書き起こす

動く手順が固まったら、コンソールでの操作を `vendorOpen()` / `vendorWrite()` / `vendorReadSync()` / `vendorControlTransfer()` の呼び出しに置き換えます。[`EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) が最小の雛形です。

書き起こす際に注意する点:

- **初期化シーケンスは順番も待ち時間も含めて再現する。** 動かないときは、キャプチャにあって省略した要求がないか確認してください。
- **ZLP（長さ0パケット）** — 転送長がMPSの倍数のとき、プロトコルによっては終端としてZLPが必要です（`vendorWriteZlp()` / `vendorSetAutoZlp()`）。
- **読み出しモード** — 要求／応答型のデバイスは `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` で開き、送ってから読みます。常時データを流してくる機器は `CONTINUOUS` です。
- **エンドポイントのMPSと転送分割** — 大きなデータは自分で分割が必要な場合があります。

### Step 9. 結果を残す

再現できたら、次の形で残しておくと後の自分と他人が助かります。

- 動く最小のスケッチを `examples/` 配下に置く（このリポジトリに追加する場合は README.md / README.ja.md / sketch.yaml も揃える）
- 手で確認する必要がある動作は `tests/manual/` にマニュアルテストとして追加する
- プロトコルの解析結果は `docs/<device>-spec.ja.md` に書き出す
- 動作を確認した機器のVID:PIDと個体差は明記する

---

## 5. プロトコルの解析

### 5.1 キャプチャの取り方

**Windows: USBPcap + Wireshark**

1. [USBPcap](https://desowin.org/usbpcap/) をインストールする（Wiresharkのインストーラに同梱されている場合もあります）
2. Wiresharkを起動し、キャプチャ対象として `USBPcap1` などのインターフェースを選ぶ。これは**ルートハブ単位**なので、対象機器がどのハブにつながっているかを確認する
3. **キャプチャを開始してから機器を挿す。** こうすると列挙の全過程が記録されます
4. 純正ソフトを起動し、解析したい操作を1つだけ行う
5. 停止して保存する

**Linux: usbmon**

```sh
sudo modprobe usbmon
sudo wireshark          # usbmonX インターフェースを選ぶ
# あるいはテキストで
sudo cat /sys/kernel/debug/usb/usbmon/0u
```

ディスクリプタだけなら `lsusb -v -d <vid>:<pid>`、HIDのレポートディスクリプタなら `usbhid-dump` が手軽です。

**macOS**: XcodeのAdditional Toolsに含まれるUSB Prober、またはWiresharkのUSBキャプチャを使います。

### 5.2 キャプチャの読み方

Wiresharkで見るときの実務的な手順です。

1. **対象デバイスだけに絞る** — `usb.device_address == 5` のようにフィルタする。アドレスは列挙時のパケットで確認できます
2. **列挙部分と通常動作部分を分ける** — 最初のGET_DESCRIPTOR群が列挙、その後が実際のプロトコルです
3. **転送種別で見る**
   - `URB_CONTROL` … setupパケットの `bmRequestType` / `bRequest` / `wValue` / `wIndex` / `wLength` を控える。これがそのまま `ctl` コマンドの引数になります
   - `URB_BULK` / `URB_INTERRUPT` … データ本体。OUT（ホスト→デバイス）とIN（デバイス→ホスト）を対で追う
4. **繰り返しを見つける** — 定期的に同じ要求が飛んでいれば、それはポーリングです。本質的な操作ではないので後回しにできます

### 5.3 差分を取るのが最短経路

未知のプロトコルを読む最も効果的な方法は、**1箇所だけ違う2つのキャプチャを比べる**ことです。

- 純正ソフトで輝度を10にしたキャプチャと、11にしたキャプチャを取る → 変化したバイトが輝度の値
- 何もしないキャプチャと、ボタンを1回押したキャプチャ → 変化した部分がイベント
- 電源ONとOFF → 状態ビット

こうして、フレームの構造（ヘッダ、コマンドID、長さ、ペイロード、チェックサム）を1バイトずつ確定させていきます。

よくあるフレーム構造の手がかり:

- 先頭の固定バイト（マジックナンバー）
- 長さフィールド（実データ長と一致する値を探す）
- シーケンス番号（1ずつ増える）
- 末尾のチェックサム（単純加算、XOR、CRC-16/MODBUSが多い。[DP100の例](dp100-spec.ja.md)）
- リトルエンディアンの16/32bit値（USBの標準フィールドはすべてリトルエンディアン）

### 5.4 注意点

- **自分が所有する機器を、自分で使うために解析する**範囲に留めてください。ソフトウェアのライセンス条項や各国の法令に反する利用は避けてください。
- キャプチャには、機器によっては入力内容（カードのID、入力文字列など）が含まれます。共有するときは中身を確認してください。
- **書き込み系のコマンドは慎重に。** 未確認のコマンドを送ると、機器の設定が壊れたりファームウェアが飛んだりする可能性があります。まず読み出し系から確定させ、書き込みは意味が確実にわかったものだけにしてください（電源装置の出力ONのような、物理的に危険なものは特に）。

---

## 6. トラブルシューティング

| 症状 | ありがちな原因 | 確認・対処 |
|------|--------------|-----------|
| `usb.begin()` が失敗する | 対象チップがUSB Host非対応／コアが古い | S2・S3・P4か、arduino-esp32が3.2.0以上（P4は3.3.1以上）か |
| 挿しても何も起きない | VBUSが出ていない | テスターで測る。セルフパワーハブを挟む |
| 挿しても何も起きない | コネクタが違う（UART側に挿している） | 回路図で確認。[`BringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) を使う |
| 挿しても何も起きない | 充電専用ケーブル | データ線入りのケーブルに替える |
| 特定の機器だけ列挙しない | コンフィグレーションディスクリプタが256バイト超 | Verboseログで確認。UVCカメラなどは対応不可（[3.5](#35-コントロール転送256バイトの壁)） |
| 特定の機器だけ列挙しない | FSでは動作しない機器 | P4のHSポートを試す（[3.2](#32-fsポートとhsポートの使い分けp4)） |
| 負荷をかけると切断される | 電流不足 | セルフパワーハブ、外部電源 |
| ESP32が再起動する | ブラウンアウト（電流不足） | 同上。ログの `Brownout detector` を確認 |
| 複数台つなぐと失敗する | チャネル不足 | `No more HCD channels available` を確認。台数を減らす（[3.4](#34-チャネル数の制限)） |
| HSポートでハブが動かない | 現状の制限 | HSポートは1台直結で使う。ハブが必要ならFSポート（[3.2](#32-fsポートとhsポートの使い分けp4)） |
| 列挙はするが `supported=no` | ライブラリに対応クラスドライバがない | [`DeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) で構成を見て、vendor APIで扱う |
| `vendorOpen()` が失敗する | そのインターフェースをライブラリが既にclaim済み | `claimed` を確認。別のインターフェース番号を指定する |
| bulk INがタイムアウトする | 要求／応答型の機器を常時ポーリングしている | `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` で開き、送信してから読む |
| 転送が途中で止まる | ZLPが必要なプロトコル | `vendorWriteZlp()` / `vendorSetAutoZlp()` |
| ハブを挿すと再起動ループ | 既知のハブ／機器の組み合わせ | [tests/manual/README.ja.md](../tests/manual/README.ja.md) の該当表を確認 |
| 原因がまったくわからない | ESP-IDF側のログが出ていない | Core Debug Level を `Verbose` にして再現する |

---

## 7. ツール一覧

### 利用者向け（examples/）

| ツール | 用途 |
|--------|------|
| [`Info/EspUsbHostBringUpCheck`](../examples/Info/EspUsbHostBringUpCheck/) | **最初に動かす。** ホストが起動するか、デバイスが列挙されるか、速度は何か。列挙されない場合のチェックリスト付き |
| [`Info/EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) | **次に動かす。** 構成の全体像、インターフェースごとの扱い方の指示、生ディスクリプタとブロック走査 |
| [`Info/EspUsbHostDeviceInfo`](../examples/Info/EspUsbHostDeviceInfo/) | 接続中の全デバイスを定期的に表示。ハブ情報とチャネル使用状況も |
| [`Info/EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/) | HIDのレポートディスクリプタ取得と簡易デコード |
| [`HID/EspUsbHostHIDRawDump`](../examples/HID/EspUsbHostHIDRawDump/) | HID入力レポートの生ダンプ |
| [`Vendor/EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/) | **プロトコル解析用。** シリアルからコントロール／バルク転送を手打ちして応答を見る |
| [`Vendor/EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) | 解析結果をスケッチに書き起こすときの雛形 |
| [`Info/EspUsbHostHubPPPS`](../examples/Info/EspUsbHostHubPPPS/) | ハブのポート電源をON/OFFして再列挙させる |
| [`Info/EspUsbHostP4FsPhyRouting`](../examples/Info/EspUsbHostP4FsPhyRouting/) | ESP32-P4のFS OTGをGPIO24/25側のコネクタへ切り替える |

### 開発者向け（tests/manual/）

pytestから実行し、ログを残す形のツールです。実行方法は [tests/manual/README.ja.md](../tests/manual/README.ja.md) を参照してください。

| ツール | 用途 |
|--------|------|
| [`smoke`](../tests/manual/smoke/) | マニュアルテストの実行環境そのものの確認。新しいPCで最初に走らせる |
| [`device_dump`](../tests/manual/device_dump/) | 全デバイスの解析済みダンプ＋未claimインターフェースのbulk/interruptエンドポイント |
| [`raw_descriptor`](../tests/manual/raw_descriptor/) | 生のDEVICE/CONFIGURATIONディスクリプタとブロック単位の走査。USBPcapとの突き合わせ用 |
| [`hid_report_descriptor`](../tests/manual/hid_report_descriptor/) | HIDレポートディスクリプタの取得 |
| [`hotplug`](../tests/manual/hotplug/) | 抜き挿しの繰り返しに耐えるか |
| [`hub_info`](../tests/manual/hub_info/) / [`hub_power`](../tests/manual/hub_power/) | ハブのトポロジ情報とポート電源制御 |
| [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/) | バルクOUTの実効スループット測定（ボードごとの上限の把握） |
| その他 | クラス別・機器別のマニュアルテスト一式（[カタログ](../tests/manual/README.ja.md)） |

---

## 8. 参考資料

- [USB 2.0 Specification](https://www.usb.org/document-library/usb-20-specification) — 規格本体
- [USB Class Codes](https://www.usb.org/defined-class-codes) — クラスコードの一覧
- [USB Device Class Documents](https://www.usb.org/documents) — HID、CDC、MSC、CCID、UACなど各クラスの仕様
- [USB Made Simple](https://www.usbmadesimple.co.uk/) — 初学者向けの解説
- [USBPcap](https://desowin.org/usbpcap/) / [Wireshark](https://www.wireshark.org/) — Windowsでのキャプチャ
- [Linux USB project](https://www.kernel.org/doc/html/latest/driver-api/usb/index.html) — カーネルドライバ（事実上の仕様書）
- [ESP-IDF USB Host Library](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host.html) — このライブラリの土台
- [動作確認済みデバイスとボード](tested-devices.ja.md) — 実機で確認した機器のVID:PIDと条件、推奨ボード
- このリポジトリの [README.ja.md](../README.ja.md) — API仕様と各クラスの対応状況
- [docs/](.) — 個別デバイス・プロトコルの解析メモ
