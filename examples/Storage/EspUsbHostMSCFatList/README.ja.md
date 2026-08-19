# EspUsbHostMSCFatList

> English: [README.md](README.md)

USB Mass Storageデバイスを`EspUsbHostMscFS`で`/usb`へマウントし、Arduino `fs::FS` / `File` APIでルートディレクトリを一覧したあと、小さな確認用ファイルを書き込み、読み戻して削除します。

このスケッチはUSBメモリへ書き込みます。書き込み・削除してよいUSBメモリを使ってください。

> **注意:** 本ライブラリのUSB MSC対応は実験的な機能です。単純な読み書き用途では問題なく利用できるはずですが、安定性や速度を重視するデータ保存には、多くの場合、SPI接続のSDカードの方が適しています。USB接続が必要な場合を除き、USB MSCを標準の保存手段として使うことはあまり推奨しません。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- FATでフォーマット済みで、書き込み・削除してよいUSB Mass Storageデバイス（USBフラッシュドライブ、USBカードリーダーなど）

## 確認できること

- `EspUsbHostMscFS::begin(usb, "/usb")`でFATをマウントし、`fs::FS`互換オブジェクトとして使う
- `EspUsbHostMscFS::mounted()`でマウント済みか確認する
- `File root = fs.open("/")`と`openNextFile()`でファイル一覧を読む
- `File::print()` / `File::readBytes()`でファイルを書き込み・読み戻しする

基本的な`fs::FS`利用では、スケッチ側で`mscReady()`、`mscWaitReady()`、`mscGetBlockDeviceInfo()`を直接呼ぶ必要はありません。MSCデバイスがまだ使えない間は`EspUsbHostMscFS::begin()`が失敗するため、少し待って再試行します。ブロックデバイスの詳細を見たい場合だけ、低レベルMSC APIを併用してください。

`EspUsbHostMscFS`は`fs::FS`を継承しているため、`fs::FS &`を受け取るWebServerやUpdateなどのArduinoライブラリへ渡せます。USBコールバック内からは呼ばず、`loop()`から使ってください。書き込み中やファイルを開いたままUSBメモリを抜いた場合、未反映データが失われる可能性があります。

FatFs同期時にSCSI `SYNCHRONIZE CACHE(10)`が失敗した場合、そのmountでは以後このコマンドを自動的にスキップします。問題が分かっている非準拠デバイスでは、`usbMassStorage.begin(...)`の前に`usbMassStorage.setSkipSyncCache(true)`を呼ぶと最初からスキップできます。互換性は上がりますが、明示的なflush動作は弱くなります。

現在のFatFs/VFSマウント経路はESP-IDF側のFatFs設定に依存し、32-bit sectorまでです。複数MSCデバイス同時接続はESP32-S3ではHCDチャネル数の制約が強いため、実用上は単一MSCデバイスを前提にしてください。

## シリアル出力例

利用可能なMSCデバイスがマウントされるまで、約1秒ごとに再試行して `USB MSC FS mount failed: ...` を表示します。マウント成功後のルート一覧は、接続したドライブの内容によって変わります。

```
connected: device: address=1 portId=0x01 vid=058f pid=6387 class=0x00(Device) speed=full product="Mass Storage Device"
Root entries:
  FILE README.TXT size=1024
  DIR  MUSIC size=0
Wrote 31 bytes to /ESPUSBHT.TST
Read back 31 bytes: EspUsbHost MSC FAT write probe
Removed /ESPUSBHT.TST
```
