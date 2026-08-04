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
- PC/SC疑似APDU `FF CA 00 00 00`（Get UID）を送り、UIDとステータスワードを表示
- カードを非活性化（`ccidPowerOff`）

スケッチ開始時に既にカードが載っている場合は挿入通知が来ないため、open直後に一度 `ccidCardPresent()` を確認しています。

## 主なAPI

- `usb.ccidOpen(address)` — CCID interfaceをclaimし、slot変化通知を開始
- `usb.ccidGetInterface(info, address)` — endpointとCCID class descriptorの値（slot数、`dwFeatures`、exchange level、`dwMaxCCIDMessageLength`）
- `usb.ccidGetStatus(status, slot, address)` / `usb.ccidCardPresent(slot, address)` — slotの状態
- `usb.ccidPowerOn(atr, capacity, &length, voltage, slot, address)` — カードを活性化しATRを取得
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
Get UID: sw=9000
UID: 6b6dccae
```
