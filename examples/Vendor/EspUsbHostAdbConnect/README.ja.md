# EspUsbHostAdbConnect

> English: [README.md](README.md)

`EspUsbHost` の汎用vendor bulk APIだけを使った、実用最小限のADB hostテンプレートです。ADBをライブラリ本体へ組み込まず、このexample内で次の共通処理を実装します。

- `ff/42/01` のADB interface検出とclaim
- ADB packetのencode、checksum、受信再構成、検証
- RSA-2048鍵の初回生成とNVSへの保存
- `AUTH TOKEN`へのSHA-1/PKCS#1 v1.5署名
- 未登録鍵の公開鍵送信とAndroidのUSBデバッグ許可
- `OPEN` / `OKAY` / `WRTE` / `CLSE` による単一stream
- `shell:echo ESP_USB_HOST_ADB_OK` の実行

これは完全なADBクライアントではありません。複数stream、対話shell、`sync:`、shell v2、forward、TLSは実装していません。

## 必要なもの

- Arduino-ESP32のUSB Hostに対応したESP32-S2/S3/P4
- USBデバッグを有効にしたAndroid端末
- データ通信対応USBケーブル

初回接続時はAndroidをロック解除し、「USBデバッグを許可しますか？」で許可してください。鍵はNVS namespace `esp-adb` の `rsa-key` に残るため、以後は同じボードから署名だけで認証できます。許可画面が出ない場合は、Androidの開発者向けオプションで「USBデバッグの許可を取り消す」を実行し、USBデバッグをOFF/ONして再接続します。

## 実行結果

成功すると次のように表示されます。

```text
ADB send: AUTH SIGNATURE
ADB send: AUTH RSAPUBLICKEY
ADB connected: version=0x01000001 maxdata=4096
ADB send: OPEN shell:echo ESP_USB_HOST_ADB_OK
ADB stream data: ESP_USB_HOST_ADB_OK
[PASS]
```

すでに鍵が許可済みなら、公開鍵送信と許可画面は省略されます。

## 実装上の重要点

`vendorWrite()` は同期的に完了を待つので、USB client task上の接続callbackから呼ばず、callbackではフラグだけを立てて`loop()`から送信します。

ADBは24 byte headerとpayloadを別々のUSB transferで送ります。payload長がBulk OUT endpointのmax packet sizeの倍数なら、payload後にzero-length packet（ZLP）が必要です。RSA署名は256 byteなので、full-speedの64 byte endpointでは特に重要です。ZLPがないと端末が次のADB headerまで前のpayloadとして待ち、認証が進みません。このスケッチは `vendorOpen()` 後に `vendorSetAutoZlp(true)` を呼び、パケット境界で終わる書き込みにライブラリ側がZLPを付けるようにしています。

受信には`onVendorData()`から専用ring bufferへコピーする方式を使います。汎用API内蔵の小さなread bufferだけに依存すると、長い`CNXN` bannerで古いbyteが失われる可能性があります。

## 拡張する場所

- 実行コマンド変更: `SHELL_SERVICE` と、完了判定用の `EXPECTED_OUTPUT` を変更
- 対話shell: streamを閉じず、送信queueを追加し、相手の`OKAY`後に次の`WRTE`を送信
- 複数stream: local IDごとにremote ID、状態、受信bufferを持つtableへ置換
- shell v2: `shell,v2,raw:`を開き、そのstream内のstdout/stderr/exit packetを解析
- file転送: `sync:` stream上へSYNCの`SEND` / `RECV` / `DATA` / `DONE`処理を追加
- logcat等: 任意のADB service stringを`A_OPEN`へ渡し、長時間streamとして処理

製品利用では、NVS暗号化や鍵の事前プロビジョニング、公開鍵fingerprintの表示、送受信timeout、再接続、複数streamのbackpressureも追加してください。このexampleは従来形式のRSA AUTHを示すためADB version `0x01000000`を提示し、STLS/TLS negotiationは行いません。

## 検証

同じ実装を使う実機manual testは次で実行できます。

```sh
cd tests
uv run --env-file .env pytest manual/adb_connect/adb_connect.py -v -s
```

Pixel 6aで、初回許可、保存鍵での認証、単一shell stream、echo応答、stream closeまで確認しています。

## 読み方と確認の手順

### 実行前の準備

ADBを認証し、範囲を限定したshellのecho試験を行う。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

受信callbackでbyteをringへコピーし、`loop()`でADB header・checksum・stream IDを検査します。Android端末をunlockし、要求されたdebug認証を確認します。秘密鍵はNVSへ保存するため、スケッチを書き直してもHostの識別が変わるとは限りません。

### APIの使い方と改造時の注意

USBのIN/OUTは常にdevice側から見た名称で、INはホストへ、OUTはdeviceへ向かいます。要求・応答というアプリの役割とendpoint方向を区別します。bulkはbyte streamであり、一転送が一命令とは限りません。実際の長さとprotocolの区切りを使います。control転送はpayloadだけでなく方向・request・value・index・lengthも持ちます。

USB転送の受付・完了はアプリの確認応答ではありません。相手のstatusも確認し、切断時にはprotocol状態を回復します。callbackのポインタは戻ると再利用される場合があります。非同期・direct bufferはacquire・submit・completionの所有規約を守り、転送中に上書きしません。queueや転送長を調整する前に短い既知の要求から試します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostAdbConnect.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
