# EspUsbHostMSCFatList

USB Mass StorageデバイスをESP-IDF FatFs/VFSで`/usb`へマウントし、ルートディレクトリを一覧したあと、小さな確認用ファイルを書き込み、読み戻して削除します。

このスケッチはUSBメモリへ書き込みます。書き込み・削除してよいUSBメモリを使ってください。

## 確認できること

- `mscWaitReady()`でMSCメディアの準備完了を待つ
- `mscGetBlockDeviceInfo()`でブロックデバイス情報を取得する
- `mscMount("/usb")`でFATをマウントする
- POSIXの`opendir()` / `readdir()` / `stat()`でファイル一覧を読む
- `fopen()` / `fwrite()` / `fread()`でファイルを書き込み・読み戻しする
- `mscUnmount("/usb")`でアンマウントする

現在のFatFs/VFSマウント経路はESP-IDF側のFatFs設定に依存し、32-bit sectorまでです。Arduino `FS`オブジェクト化はまだ行いません。
