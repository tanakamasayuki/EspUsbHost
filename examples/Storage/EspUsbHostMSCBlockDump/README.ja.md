# EspUsbHostMSCBlockDump

> English: [README.md](README.md)

USB Mass Storageデバイスの容量・デバイス情報を取得し、LBA 0の先頭64バイトをダンプするサンプルです。

MSC対応はブロックI/Oのみです。このサンプルはFATのマウントやファイル操作は行いません。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB Mass Storageデバイス（USBフラッシュドライブ、USBカードリーダーなど）

## 動作内容

- 接続時にデバイスのVID・PID・製品名を表示
- `mscInquiry()`、`mscRequestSense()`、`mscTestUnitReady()`、`mscCapacity()`でデバイス情報を取得して表示
- LBA 0の先頭64バイトをHex形式でダンプ
- LBA 0にMBRシグネチャがある場合はパーティションテーブルを解析して表示

## 注意

- MSCコマンドはUSB転送の完了まで処理をブロックします。USBイベントコールバック内ではなく、必ず`loop()`から呼び出してください。
- `mscReady()`は、デバイスがマウントされてブロックI/Oの準備ができた時点でtrueを返します。
- 接続ごとに1回だけ読み出します（`printed`フラグで制御）。再読み出しするには抜き差ししてください。
- ブロックサイズが512バイトを超える場合は情報を表示しますが、ダンプは行いません。
- MBRの検出は、LBA 0の510〜511バイト目に`0x55 0xaa`のブートシグネチャが必要です。GPTディスクやフォーマット直後のカードでは検出されないことがあります。

## 主要API

- `usb.mscReady()` — USB Mass StorageデバイスがI/O可能な状態のときtrueを返す
- `usb.mscInquiry(inquiry)` — `EspUsbHostMscInquiry`にベンダー名・製品名・リビジョン・リムーバブルフラグを格納
- `usb.mscRequestSense(sense)` — `EspUsbHostMscSense`にレスポンスコード・センスキー・ASC・ASCQを格納
- `usb.mscTestUnitReady()` — メディアが存在してI/O可能なときtrueを返す
- `usb.mscCapacity(blockCount, blockSize)` — 総ブロック数とブロックサイズ（バイト）を返す
- `usb.mscReadBlocks(lba, buffer, count)` — `lba`から`count`ブロックを`buffer`へ読み出す
- `usb.lastErrorName()` — 直近のエラーを表す人が読める文字列を返す

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=058f pid=6387 class=0x00(Device) speed=full product="Mass Storage Device"
MSC inquiry: removable=1 vendor='VendorCo' product='ProductCode' revision='1.00'
MSC sense: response=0x70 key=0x00 asc=0x00 ascq=0x00
MSC test unit ready: 1
MSC capacity: blocks=7831552 block_size=512 total_kib=3993600
LBA0:
0000: eb 58 90 4d 53 44 4f 53 35 2e 30 00 02 08 20 00
0016: 02 00 00 00 00 f8 00 00 3f 00 ff 00 00 08 00 00
0032: 00 08 77 00 f4 02 00 00 00 00 00 00 02 00 00 00
0048: 01 00 06 00 00 00 00 00 00 00 00 00 00 00 00 00
MBR partitions:
  #1 boot=0x00 type=0x0b first_lba=8192 sectors=7823360 size_kib=3911680
```
