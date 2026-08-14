# 動作確認済みデバイスとボード

> English: [tested-devices.md](tested-devices.md)

このリポジトリで実機確認した個体を1枚にまとめたものです。購入前の判断と、動かないときの「自分の機器が悪いのか、環境が悪いのか」の切り分けに使ってください。

情報の出所は、README・各サンプルのREADME・`tests/manual` のカタログ・`docs/` の解析メモです。**ここに載っていない機器が動かないという意味ではありません。** 載っているのは「誰かが実際に確認した個体」だけです。

## 読み方

| 記号 | 意味 |
|------|------|
| ✅ | 実機で確認済み |
| ⚠️ | 条件つきで動作（速度・接続方法・設定の制約あり） |
| ❌ | 動作しないことを確認済み |

注意点:

- 特記のない限り、確認は **ESP32-S3（フルスピード）** で行っています。
- **VID:PIDが同じでもファームウェアが違えば挙動は変わります。** 特にホワイトラベル製品（後述のマクロパッドなど）は同一設計が多数のVID:PIDで売られています。
- 未知の機器をつないだときにまず何を見るかは [USB Host開発ガイド](usb-host-guide.ja.md) を参照してください。

---

## 入力デバイス（HID）

| 機器 | VID:PID | 状態 | 内容 | 参照 |
|------|---------|------|------|------|
| USBキーボード（一般） | — | ✅ | ブートプロトコル、NKRO（ビットマップレポート）、LED制御。各国レイアウト変換はホスト上の単体テストで検証 | [`keyboard_leds`](../tests/manual/keyboard_leds/) / [`tests/unit/keymap`](../tests/unit/) / [HID](../examples/HID/) |
| USBマウス（一般） | — | ✅ | 移動・ボタン・ホイール | [HID](../examples/HID/) |
| キーボード＋マウス同時接続 | — | ✅ | 2台が独立してイベントを出す | [`multi_hid_keyboard_mouse`](../tests/manual/multi_hid_keyboard_mouse/) |
| ゲームパッド | — | ✅ | 軸・ハット・ボタン。レポートディスクリプタからのデコード | [`EspUsbHostGamepad`](../examples/HID/EspUsbHostGamepad/) |
| ALIENTEK DP100 電源 | `2e3c:af01` | ⚠️ | HIDレポートに載った独自プロトコル。読み出しは検証済み、設定値書き込みは実装済み。マニュアルテストは**直結**が前提。ハブ経由はハブ依存で、CH335Fとの組み合わせは再起動ループ、RTD5411では問題なし（下記ハブの項） | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) / [dp100-spec.ja.md](dp100-spec.ja.md) |
| STREONOR S6 LCDマクロパッド | `1500:3006` | ⚠️ | 6キーのLCD描画・輝度・キー入力。**ESP32-P4のHSポート専用**（interrupt OUTが1024バイト） | [`EspUsbHostMacroPadN3`](../examples/HID/EspUsbHostMacroPadN3/) |

マクロパッドは同一設計が多数のブランドで流通しています（Mirabox Stream Dock N3 `6602:1000` / `6602:1002` / `6603:1002` / `6603:1003`、Ajazz AKP03系 `0300:300x`、ホワイトラベル `1500:3001`）。サンプルはVID:PIDではなく**インターフェース形状**で判定します。

---

## USBシリアル（CDC / VCP）

TX-RX を短絡したループバックで確認しています。

| チップ | VID:PID | 状態 | 参照 |
|--------|---------|------|------|
| CDC-ACM 一般 | — | ✅ | [`EspUsbHostUSBSerial`](../examples/Serial/EspUsbHostUSBSerial/) |
| FTDI（FT232R等） | VID `0x0403` | ✅ | [`vcp_ftdi`](../tests/manual/vcp_ftdi/) |
| CP210x（CP2102等） | VID `0x10C4` | ✅ | [`vcp_cp210x`](../tests/manual/vcp_cp210x/) |
| CH34x（CH340等） | VID `0x1A86` | ✅ | [`vcp_ch34x`](../tests/manual/vcp_ch34x/) |
| PL2303 | `067B:2303` | ✅ | [`vcp_pl2303`](../tests/manual/vcp_pl2303/) |
| PL2303GS | `067B:23A3` | ✅ | [`vcp_pl2303gs`](../tests/manual/vcp_pl2303gs/) |
| ESP32ボード（自動リセット対象） | — | ✅ | [`esp32_autoreset`](../tests/manual/esp32_autoreset/) — DTR/RTSによるリセットとROMダウンロードモード遷移 |

複数同時接続はハブとチャネル数の影響を受けます。ESP32-S3でハブ経由の実測では、**FTDI + CP210x は動作、FTDI + CH34x はチャネル不足で失敗**しました（[`multi_serial`](../tests/manual/multi_serial/)）。

---

## ストレージ（MSC）

| 機器 | 状態 | 内容 | 参照 |
|------|------|------|------|
| USBメモリ（一般） | ⚠️ | 容量取得、LBA読み出し、FatFs/VFSマウント、POSIX と `fs::FS` での読み書き | [`msc_block`](../tests/manual/msc_block/) |
| USBメモリ（活線挿抜） | ⚠️ | マウント中に抜いた後、同じパスへ再マウント可能 | [`msc_hotplug_mount`](../tests/manual/msc_hotplug_mount/) |

⚠️ の理由は、MSC自体が実験的扱いであるためです。複数MSC機器、複数LUN、特殊なブロックサイズ、異常系のBOT復帰は検証が不足しています。非準拠デバイスは `SYNCHRONIZE CACHE(10)` のフォールバックが必要になることがあります。

ESP32-P4ではCPUキャッシュとUSB DMAの整合性が問題になり得るため、[`msc_cache_coherency`](../tests/manual/msc_cache_coherency/) で専用に検証しています（ライブラリ側で対処済み、アプリ側の対応は不要）。

---

## スマートカードリーダ（CCID）

| 機器 | VID:PID | 状態 | 内容 | 参照 |
|------|---------|------|------|------|
| Sony RC-S300（FeliCa Port/PaSoRi 4.0） | `054c:0dc8` | ✅ | オープン、クラスディスクリプタ、スロット状態、ATR付き電源投入、APDU交換、カード挿抜通知 | [`Ccid`](../examples/Ccid/) / [ccid-api-spec.ja.md](ccid-api-spec.ja.md) |
| Sony RC-S300 + FeliCaカード | 同上 | ✅ | トランスペアレントセッションでSystem Code指定のPolling、IDm取得 | [`EspUsbHostCcidFelicaIdm`](../examples/Ccid/EspUsbHostCcidFelicaIdm/) |

ICCD派生、チェーン（拡張APDU）応答、PINパッド機能は対象外です。

---

## オーディオ・MIDI

| 機器 | 状態 | 内容 | 参照 |
|------|------|------|------|
| USB MIDI機器 | ✅ | MIDI入出力、ケーブル（仮想ポート）数の取得 | [`EspUsbHostMIDI`](../examples/MIDI/EspUsbHostMIDI/) |
| Arduino標準 `USBAudioCard`（UAC1ペア） | ⚠️ | アイソクロナスIN/OUTのストリーミング | [Audio](../examples/Audio/) |
| EspUsbDevice ペア（UAC2） | ⚠️ | ディスクリプタ、Clock Sourceのサンプルレート、Feature Unitのミュート/音量、IN/OUTストリーミング | 同上 |

**実機のUSBマイク／オーディオインターフェースでの検証はまだ不足しています。** また、ESP32-S2/S3のホストはフルスピードのみなので、**FS用コンフィグを持たないUAC2機器はそもそも使えません。**

---

## USB Ethernet（CDC-NCM / CDC-ECM）

| 機器 | 状態 | 内容 | 参照 |
|------|------|------|------|
| ASIX AX88179A アダプタ | ⚠️ | ネットワーク機能が既定のコンフィグに無いため、`setConfigurationSelector()` と2パス列挙が必要（arduino-esp32 3.3.11以上） | [UsbNetwork](../examples/UsbNetwork/) / [usb-network-spec.ja.md](usb-network-spec.ja.md) |
| EspUsbDevice の NCM デバイス | ✅ | ペアテストによる送受信、lwIP netif接続 | 同上 |

任意のアダプタで動くかは、そのアダプタがどのコンフィグにCDC-NCM/ECMを置いているかに依存します。[`usb_network_descriptor`](../tests/manual/usb_network_descriptor/) が全コンフィグを走査して候補を表示します。

---

## ディスプレイ

| 機器 | VID:PID | 状態 | 内容 | 参照 |
|------|---------|------|------|------|
| DisplayLink DL-1xx（USB to DVI-17） | `17e9:0360` | ⚠️ | EDID読み出し、1920x1080モード設定、16bpp描画。LovyanGFXパネルとして動作 | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) / [usb-display-spec.ja.md](usb-display-spec.ja.md) |
| AX206 USBフォトフレーム | `1908:0102` | ⚠️ | 480x320。全画面転送のみのため、1フレーム=307,200バイトを1トランザクションで送る。**ESP32-S3で約2fps** | [`EspUsbHostDisplayAx206`](../examples/Vendor/EspUsbHostDisplayAx206/) |
| 3.5インチUSBスマートスクリーン（`USB35INCHIPSV2`） | `1a86:5722` | ⚠️ | CDCシリアル上の独自プロトコル、16bpp、部分転送・輝度制御 | [`EspUsbHostDisplayTuring`](../examples/Serial/EspUsbHostDisplayTuring/) |

いずれも「サンプル実装（best effort）」の扱いで、1機種のリファレンス実装です。フレームレートはバス帯域が上限になります（FSで約1.1MB/s、HSで約36MB/s。[上級編 4.4](usb-host-advanced.ja.md#44-理論帯域と実測)）。

ESP32-P4でDL-1xxを使う場合、セルフパワーハブが必要で、かつそのハブ上で単独である必要があります。

---

## 計測器・プリンタ・その他

| 機器 | VID:PID | 状態 | 内容 | 参照 |
|------|---------|------|------|------|
| KIKUSUI PMX18-5A 直流電源（USBTMC） | `0b3e:1029` | ⚠️ | クラス要求、バルクメッセージ層、SCPI問い合わせ、20連続クエリ、CLEAR。USB488の割り込みINは未使用 | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/) / [usbtmc-spec.ja.md](usbtmc-spec.ja.md) |
| Xprinter XP-C58K レシートプリンタ | `0483:070b` | ⚠️ | クラス要求3種、ESC/POSリアルタイムステータス、日本語レシート・バーコード・QR・オートカット。**クラス要求は応答が空**なので `DLE EOT n` を状態取得に使う | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/) / [printer-spec.ja.md](printer-spec.ja.md) |
| Androidスマートフォン（ADB） | — | ⚠️ | USBデバッグ有効・データ用ケーブル必須。RSA鍵での認証とシェルストリーム | [`EspUsbHostAdbConnect`](../examples/Vendor/EspUsbHostAdbConnect/) |

---

## USBハブ

| 機器 | VID:PID | 状態 | 内容 |
|------|---------|------|------|
| セルフパワー USB 2.0 ハブ（一般） | — | ✅ | 複数デバイスの同時利用、トポロジ表示 |
| PPPS対応ハブ | — | ✅ | ポート単位の電源ON/OFFとデバイスの再列挙（[`hub_power`](../tests/manual/hub_power/)） |
| RTD5411 | `0bda:5411` | ✅ | DP100を含む構成で問題なし |
| CH335F | `1a86:8094` | ⚠️ | 一般的なHID・USBメモリ・PMX18-5Aでは問題なし |
| **CH335F + ALIENTEK DP100** | `1a86:8094` + `2e3c:af01` | ❌ | **ESP-IDFのハブドライバが落ちて再起動ループ**（`ext_hub.c` のassert）。このライブラリの動作とは無関係で、ハブ追跡を無効にしても発生する |

ハブ関連の既知の問題は [tests/manual/README.ja.md](../tests/manual/README.ja.md) に詳細があります。原因の切り分けには [`tests/probe/hub_enum`](../tests/probe/) を使ってください。

**ESP32-P4のHSポートでは、ハブは実質的に使えません。** ハブが必要な構成はFSポートを使ってください（[入門編 3.2](usb-host-guide.ja.md#32-fsポートとhsポートの使い分けp4)）。

---

## 動作しないことが確認されている機器

| 機器 | 理由 |
|------|------|
| USBカメラ全般（UVC、クラス `0x0e`） | Arduino-ESP32のビルド済みホストスタックが `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` のため、コンフィグレーションディスクリプタが256バイトを超えるデバイスは列挙自体に失敗する。**Logitech C920 は1,974バイト**。スケッチやライブラリの設定では回避できない |
| USB 3.x（SuperSpeed）専用機器 | ESP32はUSB 2.0まで。USB 2.0にフォールバックしない機器は使えない |
| HS専用・FS構成を持たない機器（ESP32-S2/S3） | S2/S3のホストはフルスピードのみ。ESP32-P4のHSポートなら動作する可能性がある |
| 1024バイトエンドポイントを持つ機器（FSポート） | フルスピードにその選択肢が無い。ESP32-P4のHSポート＋FIFO再分割が必要（[上級編 5.2](usb-host-advanced.ja.md#52-fifo分割)） |

---

## ボード

USB Hostとして使う前に、**そのコネクタのVBUSに5Vが出ているか**を必ず確認してください。

| ボード | VBUS出力 | 備考 |
|--------|---------|------|
| Espressif ESP32-S3-DevKitC-1 | ❌ 出ない | USB OTGコネクタから接続デバイスへ給電しない。外部電源かセルフパワーハブが必要 |
| Freenove ESP32-S3-WROOM Board | ✅ 出る | OTG Type-Cから給電可能。最初の1枚として推奨 |
| M5Stack製品の一部 | ⚠️ 製品による | USBコネクタの電源をソフトウェア制御できる製品がある。回路図と手順を確認 |
| AtomS3 など単一コネクタ製品 | ⚠️ 製品による | 最終製品には使えるが、開発中はコネクタが2つあるボードを推奨 |

開発中は、書き込み・シリアルモニタ用（USB-UART）とホスト用（OTG）でコネクタが分かれているボードが扱いやすくなります。**どちらのコネクタがどちらかはボードごとに異なり**、ESP32-S3-DevKitC-1とFreenoveでは位置が逆です。シルク印刷ではなく回路図で確認してください。

ESP32-P4はUSB機能を3つ（USB Serial/JTAG、OTG FS、OTG HS）持ち、ボードによる差が大きいので、[README.ja.md のESP32-P4に関する注意](../README.ja.md)と [`tests/probe/`](../tests/probe/) のポート特定用スケッチを参照してください。

---

## この一覧への追記

新しい機器で動作を確認したら、次を記録してください。あとから読む人が再現できる粒度が目安です。

- VID:PID と製品名（可能ならファームウェアバージョン）
- 確認したターゲット（ESP32-S3 / P4）とポート（FS / HS）
- 接続方法（直結 / ハブ経由 / セルフパワーハブ必須）
- 何を確認して、何を確認していないか
- 参照先（サンプル、マニュアルテスト、解析メモ）

確認手順そのものは [USB Host開発ガイド](usb-host-guide.ja.md) の「実験の進め方」を参照してください。
