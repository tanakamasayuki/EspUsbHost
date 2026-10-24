# EspUsbHostCcidReader

> English: [README.md](README.md)

CCIDスマートカードリーダー用APIのサンプルです。`bInterfaceClass == 0x0b` のinterfaceのclaim、カードの挿入・排出通知、カードの活性化とATR取得、APDU送受信を行います。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Host対応の他のボード）
- CCIDスマートカードリーダー — Sony RC-S300（`FeliCa Port/PaSoRi 4.0`、VID 0x054c PID 0x0dc8）で確認
- スマートカード。非接触リーダーの場合は任意のISO 14443カード（交通系ICカード、社員証など）

## 動作

- 接続時にCCID interfaceをclaim（`ccidOpen`）し、リーダーのclass descriptorの内容を表示
- リーダーのinterrupt IN endpointから届くカードの挿入・排出を表示
- カードを活性化（`ccidPowerOn`）してATRを表示
- ATRから判定したカードの規格と名称を表示（`ccidGetCardInfo`）
- PC/SC疑似APDU `FF CA 00 00 00`（Get UID）を送り、UIDとステータスワードを表示
- カードを非活性化（`ccidPowerOff`）

スケッチ開始時に既にカードが載っている場合は挿入通知が来ないため、open直後に一度 `ccidCardPresent()` を確認しています。

## カード種別について

カードの規格はATRから判定します。非接触のストレージカードは自前のATRを持たないため、
PC/SC準拠のリーダーがPC/SCのRID `A0 00 00 03 06`・標準バイト（ISO 14443 A/B、
ISO 15693、FeliCaなど）・カード名を載せた合成ATRを生成します。`ccidGetCardInfo()` は
これを読んでいます。自前のATRを返すカード（接触カードや、ISO 14443-4で話す非接触
カード）にはこの識別情報が無いため、`ISO 7816 card (own ATR)` として報告します。historical bytes
が1バイトも無いATRは何も識別できないため `unknown` のままにします。

その場合は `ccidIdentifyCard()` がカード自身に問い合わせます。Get UIDを送り、識別子の形
から規格を推定します（8バイトはFeliCaのIDm、先頭 `0xe0` ならISO 15693のUID。7・10バイト
はISO 14443 AのNFCID1。4バイトで先頭 `0x08` はスマートフォンが提示するランダムNFCID1。
それ以外の4バイトはISO 14443のtypeを確定しません）。識別子の形からの推測なので、
`info.fromUid` で区別できます。

## 主なAPI

- `usb.ccidOpen(address)` — CCID interfaceをclaimし、slot変化通知を開始
- `usb.ccidGetInterface(info, address)` — endpointとCCID class descriptorの値（slot数、`dwFeatures`、exchange level、`dwMaxCCIDMessageLength`）
- `usb.ccidGetStatus(status, slot, address)` / `usb.ccidCardPresent(slot, address)` — slotの状態
- `usb.ccidPowerOn(atr, capacity, &length, voltage, slot, address)` — カードを活性化しATRを取得
- `usb.ccidGetCardInfo(info, slot, address)` — ATRから判定したカードの規格（ISO 14443 A/B・ISO 15693・FeliCaなど）、レベル、カード名
- `usb.ccidIdentifyCard(info, slot, address)` — 上記に加え、ATRで判定できないカードにGet UIDで問い合わせる
- `usb.ccidApdu(apdu, length, response, capacity, &responseLength, &statusWord, slot, address)` — SW1SW2を分離したAPDU送受信
- `usb.ccidTransfer(...)` — ステータスワードを分離しない生の `PC_to_RDR_XfrBlock`
- `usb.ccidEscape(...)` — リーダー固有コマンド
- `usb.ccidMessage(type, messageSpecific, data, length, response, slot, address)` — 上記以外のCCIDメッセージ
- `usb.onCcidCardInserted(callback)` / `usb.onCcidCardRemoved(callback)` — slot変化通知。USB task上で呼ばれる

コマンドは同期APIのためUSB callbackからは呼べません。このサンプルでもcallbackはフラグを立てるだけで、実際の処理は `loop()` で行っています。

## 期待されるシリアル出力

```
EspUsbHost CCID reader example start
connected: address=2 vid=054c pid=0dc8 product="FeliCa Port/PaSoRi 4.0"
CCID reader ready: address=2 interface=0 slots=1 exchange=extended APDU interrupt=yes
ATR: 3b8f8001804f0ca000000306030001000000006a
card: ISO 14443 A level 3, MIFARE Classic 1K (0x0001)
Get UID: sw=9000
UID: 6b6dccae
```

## 読み方と確認の手順

### 実行前の準備

カードを活性化し、ATRとUID要求への応答を調べる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

挿入通知では処理を予約し、`loop()`でreaderを開いてpower-on・APDUを実行します。通信の成否、応答長、status wordを分けて見ます。PC/SCのGet UID疑似APDUはreader依存で、すべての接触・非接触カードの共通命令ではありません。

### APIの使い方と改造時の注意

readerの列挙、interface開始、カード有無、活性化・ATR、APDUの各段階を分けます。CCID転送の成功とアプリのstatus wordは別の問いです。出力容量と実応答長の検査を残し、カード不在を空の正常応答として解釈しないようにします。

reader固有の疑似APDU・escapeは全スマートカードの共通命令ではありません。正確なreader・カードの組合せを確認して再利用します。callbackを短く保ち、完了待ちのreader操作はスケッチのアプリ側経路で行います。callback後も必要なデータはコピーします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostCcidReader.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
