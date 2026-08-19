# EspUsbHostMacroPadN3

> English: [README.md](README.md)

Mirabox N3 / Ajazz AKP03系のLCDマクロパッドをESP32-P4から駆動します。6つのキー画面を描き、バックライトを設定し、キーとノブの入力を読みます。純正ソフトは一切使いません。

> **状態: 動作確認済み（STREONOR S6 `1500:3006` 実機）。** 実機で確認したもの: interface / endpoint構成、firmwareバージョン読み出し（`V3.S6.02.011`）、輝度・クリア・リフレッシュ、6キーへの64x64 JPEGアップロードとパネル表示、キー番号の並び、パネルがアップロード画像に加える90°回転、12個すべてのコントロールのコード。セッションのハンドシェイクとkeepalive間隔は、同じ個体を純正アプリが駆動しているUSBキャプチャから読み取ったものです。[実機で確認した内容](#実機で確認した内容)を参照。

| ファイル | 内容 |
|---|---|
| `MacroPadN3Protocol.hpp` | ワイヤフォーマット。`CRT`パケット、コマンド文字、入力レポートのオフセット。Arduino / USB依存なし |
| `MacroPadN3Device.hpp` | vendor HID interfaceの探索、interrupt OUTへのパケット送信、interrupt INで届くレポートのデコード |
| `KeyImage.hpp` | RGB565タイルを描き、ESP32-P4のJPEGペリフェラルでJPEGにエンコード |
| `EspUsbHostMacroPadN3.ino` | 接続、firmwareバージョン読み出し、6キーの描画、入力イベント表示 |

## 対象ハードウェア

これらのパッドは同一の白ラベル設計が多数のブランドで売られているもので、descriptorもproduct string（`HOTSPOTEKUSB HID DEMO`）も同じで、VID:PIDだけが違います。

| ブランド / 型番 | VID:PID |
|---|---|
| STREONOR S6 | `1500:3006`（このexampleの実機） |
| Mirabox Stream Dock N3 | `6602:1000`, `6602:1002`, `6603:1002`, `6603:1003` |
| Ajazz AKP03系 | `0300:300x` |
| ノーブランド / 白ラベル | `1500:3001` |

そのためVID/PIDでの識別は当てになりません。`MacroPadN3Device::begin()`はinterfaceの形で判定します。interrupt INと1024バイト以上のinterrupt OUTを両方持つ、claim済みのHID interfaceです。特定の個体に限定したい場合は`begin()`にvid/pidを渡します。

## ESP32-P4限定、かつHigh-speedポート限定

このパッドのvendor HID interfaceは**1024バイトのinterrupt OUT**を使います。High-speed USBとしては規格上まったく合法ですが、ESP-IDFのhost driverが既定で確保できるサイズを超えており、Full-speedポートではそもそも扱えません。

```
Interface 0  vendor HID    EP 0x82 IN  interrupt  512 B / 1 ms
                           EP 0x03 OUT interrupt 1024 B / 1 ms   <- ここが問題
Interface 1  boot keyboard EP 0x81 IN  interrupt    8 B / 10 ms
```

Interrupt / isochronousのOUTパケットはcontrollerのperiodic TX FIFOに置かれ、driver既定の分割ではそこが512バイトしかありません。結果としてinterface 0のclaimが失敗し、host driverが次を出力します。

```
E HCD DWC: EP MPS (1024) exceeds supported limit (512)
E USBH: EP Alloc error: ESP_ERR_NOT_SUPPORTED
E USB HOST: Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

スケッチ側でFIFOを再分割して解決します。

```cpp
EspUsbHostConfig cfg;
cfg.port = ESP_USB_HOST_PORT_HIGH_SPEED;
cfg.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // 周期OUTに1120 B
usb.begin(cfg);
```

他の分割については最上位[README.ja.md](../../../README.ja.md)のエンドポイントサイズ上限の節を参照してください。ESP32-S3やESP32-S2ではこのexampleは動きません。Full-speedポートのFIFOは合計1 kBしかないため1エンドポイントに1024バイトは入らず、そもそもFull-speedリンクでは1024バイトのinterruptパケットを転送できません。

`PSRAM=enabled`も必要です。P4のJPEGエンコーダがバッファをPSRAMから確保するためです。`sketch.yaml`のprofileは両方を設定しています。

## プロトコル

host→deviceのパケットは、1回1024バイトのinterrupt OUT転送です。

```
"CRT" 00 00 | ASCIIコマンド | 引数 | 1024バイトまでゼロ埋め
```

コマンドは長さ固定ではないASCIIです。多くは3文字ですが、`CONNECT`は7文字、`QUCMD`は5文字です。

| コマンド | 引数 | 意味 |
|---|---|---|
| `DIS` | — | **セッション開始。** 純正アプリが最初に送るコマンド |
| `CONNECT` | — | **keepalive。** 純正アプリは約10秒ごとに送る |
| `LIG` | `00 00 <percent>` | バックライト輝度 0..100（純正アプリは25） |
| `QUCMD` | `1f 11 00 11 00 11` | 起動時に1回だけ送られる。用途不明で、無くても動作する |
| `CLE` | `00 00 00 <key>` | キー1つの画像をクリア。`0xff`で全キー |
| `STP` | — | アップロードした内容を表示（デバイスは自分では再描画しない） |
| `BAT` | `<size:4 BE> <key>` | キー画像が続く。`size`バイトのJPEGを生の1024バイトパケットで送る |
| `LOG` | `<size:4 BE> 01` | 起動ロゴが続く。パネル全面サイズの生BGR888 |

### セッションを開かないとデバイスは何も返さない

`DIS`を送るまで、このパッドはスタンドアロンのデバイスです。自分でキーを処理し、自分のアイコンを描き、**入力レポートを一切送りません**。`DIS`で画面がホストに渡り、レポートが始まります。同時にタイマーも動き出し、セッションを無音のまま放置すると約30秒後にバスから離脱して再enumerateされるので、`CONNECT`を送り続ける必要があります。スケッチは純正アプリの半分の間隔、5秒ごとに送ります。

このデバイスに最初に手を付けたときに「壊れているように見える」原因はここです。アップロードは通り、タイルも出るのに、何も報告されず、キーを触った瞬間に自前のアイコンに戻ります。

firmwareバージョンはパケットではなくclass control transfer、vendor HID interfaceへの`Get_Report`（Input, id 0）で読みます。

```cpp
usb.vendorControlTransfer(0xa1, 0x01, 0x0100, interfaceNumber, buffer, sizeof(buffer), &actual, address);
// -> "V3.S6.02.011"
```

先頭フィールドがプロトコルバージョンで、最初に読む価値があります。このexampleが実装しているのはバージョン3（1024バイトパケット、押下と離しが別状態）です。バージョン1のデバイス（Mirabox 293系）は512バイトパケットなので、移植する場合は`MacroPadN3Protocol.hpp`の`PACKET_SIZE`が起点になります。

### 入力レポート

512バイトのinterrupt INで、**セッション中のみ**届きます。

```
"ACK" 00 00 "OK" 00 00 | code | state | ゼロ埋め
        オフセット9 ------^       ^------ オフセット10
```

| コード | 状態 | 意味 |
|---|---|---|
| `0x01`..`0x06` | `01` / `00` | LCDキー1..6の押下 / 離し |
| `0x25`, `0x30`, `0x31` | `01` / `00` | シーンキー1..3の押下 / 離し |
| `0x90` / `0x91` | `00` | ノブ1の回転、反時計回り / 時計回り |
| `0x60` / `0x61` | `00` | ノブ2の回転 |
| `0x50` / `0x51` | `00` | ノブ3の回転 |
| `0x33`, `0x34`, `0x35` | `01` / `00` | ノブ1..3の押し込み、押下 / 離し |

上記のコードはすべて、STREONOR S6の12個のコントロールを既知の順序で全部操作して確定させたものです。コード間に算術的な規則性はないので、`MacroPadN3Protocol.hpp`ではテーブルとして持っています。

ノブの番号はパッド側の割り当てと一致しています。ノブ1が左下、2が右下、3が上です。他ブランドの個体では同じコードが別の位置に割り当てられている可能性があるので、位置が重要なスケッチは自分の個体で確認してください。

### `onHIDVendorInput()`ではなく`onHIDInput()`を使う

ライブラリのvendor HID経路は、レポートの1バイト目がライブラリのvendor report IDのときだけ発火します。このデバイスのレポートは`"ACK"`始まりなので`onHIDVendorInput()`は永久に発火せず、全HIDレポートが届く`onHIDInput()`をデバイスアドレスとinterface番号でフィルタして使います。[`EspUsbHostDp100Power`](../EspUsbHostDp100Power/)が同じ形になっているのも同じ理由です。

### キー画像

`BAT`と`CLE`のキー番号は読み順で1..6です。

```
1 2 3
4 5 6
```

パネルはアップロードしたタイルを90°回して表示するため、横向きに描いたバーはキー上で縦に伸びて見えます。`KeyImage.hpp`は`drawPixel()`でこれを打ち消しており（このデバイスでは`KEY_IMAGE_ROTATION`が90）、スケッチ側の座標はxが右、yが下のまま素直に使えます。この値はデバイスごとの値で、確認方法はパネルを見ることだけです。このexampleが描くテストパターンは意図的に非対称なので、一目で判定できます。

画像自体は64x64のJPEGです。プロトコルは寸法を運ばず（`BAT`ヘッダはバイト数だけ）、サイズを間違えるとエラーではなく拡縮された絵として出ます。

### キー画像の所有権

セッション外では、押されたキーの上にパッドが自分の内蔵アイコンを描くため、アップロードしたタイルが消えます。セッションが無いときの最も分かりにくい症状がこれです。アップロードは完全に成功しているのに絵が消えるからです。

セッション中は押してもタイルが残るので、このexampleは一度アップロードしたら放置します。純正アプリはそれに頼らず、キャプチャでも毎秒数回のペースで再アップロード＋リフレッシュを繰り返していました。上書きしてくるファームウェアに対して保険をかけたいスケッチは、入力コールバックから該当キーを描き直せます（1枚のアップロードでバス時間約8 ms）。

### 無音になるとセッションが切れる

セッション中にパケットが来なくなると、パッドはバスから離脱し、数秒後に新しいアドレスで戻ってきます（実測: `DIS`だけ送って放置した2例で24秒後と36秒後）。keepaliveが防いでいるのはこれです。ハードウェア故障とまったく同じ見え方をするので、知っておく価値があります。

## 実機で確認した内容

STREONOR S6（`1500:3006`、firmware `V3.S6.02.011`）とESP32-P4のHigh-speedポートで測定した結果です。

```
connected address=1 vid=1500 pid=3006 product="HOTSPOTEKUSB HID DEMO"
pad ready address=1 interface=0
firmware="V3.S6.02.011"
key 1: 1438 byte JPEG sent
key 2: 1479 byte JPEG sent
key 3: 1318 byte JPEG sent
key 4: 1360 byte JPEG sent
key 5: 1363 byte JPEG sent
key 6: 1428 byte JPEG sent
```

- 6タイルすべてがパネルに正しい向きで表示され、枠とキー番号ぶんのバーがスケッチで描いた位置に出ました。これで64x64というサイズ、`KEY_IMAGE_ROTATION`が90であること、キー順が読み順の1..3・4..6であることが確定しました。
- セッション中はキーを押してもタイルが残り、全コントロールが報告されます:
  ```
  key 1 down        raw=41434b00004f4b000001010000000000
  key 1 up          raw=41434b00004f4b000001000000000000
  scene key 1 down  raw=41434b00004f4b000025010000000000
  encoder 1 turn +1 raw=41434b00004f4b000091000000000000
  encoder 1 down    raw=41434b00004f4b000033010000000000
  ```
- 輝度・クリア・6枚のアップロード・リフレッシュを通してデバイスはバス上に留まり、転送エラーは出ませんでした。
- interfaceをclaimしたまま何も送らないアイドル状態: 110秒間安定。
- このデバイスがconfiguration descriptorで要求する`450 mA`は、bus poweredのhubポートの割り当てを超えます。特にバックライトを上げる場合は、500 mA以上を維持できる電源をhostポートに用意してください。

### 純正アプリは何をしているか

セッションのハンドシェイクとkeepalive間隔は、同じ個体を純正アプリが駆動しているUSBキャプチャ（40秒間、USBフレーム55k）から読み取りました。起動時のシーケンスは以下のとおりです（時刻はキャプチャ相対）。

```
282.887  CRT..DIS                         セッション開始
282.888  CRT..LIG 00 00 19                輝度 0x19 = 25
282.889  CRT..QUCMD 1f 11 00 11 00 11     用途不明
282.945  CRT..CLE 00 00 00 ff             全キークリア
282.953  CRT..BAT 00 00 10 7f 01          キー1、JPEG 4223バイト
282.958  CRT..BAT 00 00 0e 81 02          キー2、3713バイト
   ...   あと4回                          キー3〜6
282.979  CRT..STP                         表示
```

その後、パッドを放置している間:

```
293.458  CRT..CONNECT
302.879  CRT..CONNECT     9.42秒
312.914  CRT..CONNECT    10.03秒
322.879  CRT..CONNECT     9.97秒
```

トラフィックからもう2点わかります。純正アプリは画像がデバイス側に保持されることに頼っていません。この40秒間に `BAT` を3151回、`STP` を3131回、毎秒数回のペースで送っており、キー画像のサイズは2110〜5025バイトでした。そしてアップロード先は6つではなく**7つ**あります。キーID 1〜6 に加えて `0x0b` があり、キー5・6と同じ頻度で書かれています。

未確定:

- `QUCMD`が何を問い合わせているか。`BAT`のキーID `0x0b` がどこを指すか（純正アプリはキー5・6と同じ頻度でここへアップロードしているので、6キー以外にも画像ターゲットがあります）。
- 起動ロゴ。`LOG`はプロトコルどおり実装してありますが、パネル全面のサイズが不明なので、`setBootImage()`は正しいサイズのバッファを呼び出し側が用意する前提で、未検証です。

## 参照元

ここでのプロトコル記述は公開されたリバースエンジニアリング成果に基づきます。参照したのはpermissiveライセンスの資料だけです。

- [rigor789/mirabox-streamdock-node](https://github.com/rigor789/mirabox-streamdock-node)（MIT）— `CRT`パケットの構造とコマンド文字。MiraBox 293（プロトコルバージョン1）由来。
- [4ndv/mirajazz](https://github.com/4ndv/mirajazz) — READMEに、どのデバイスがどのプロトコルバージョンかと、バージョン2/3が1024バイトパケットを使うことが書かれています。ライブラリ本体はMPL-2.0で、コードは使用していません。
- [4ndv/opendeck-akp03](https://github.com/4ndv/opendeck-akp03) — このデバイス系向けのOpenDeckプラグイン。対応デバイスの一覧を追うならここです。GPL-3.0で、コードは使用していません。

上記以外の内容は、`tests/manual/device_dump`とこのディレクトリのスケッチで実機測定したものです。

Mirabox、Stream Dock、Ajazz、STREONORはそれぞれの権利者の商標です。本プロジェクトはこれらと提携しておらず、これらによる承認や認証も受けていません。
