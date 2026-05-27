# EspUsbHostMSCFatList

USB Mass Storageデバイスを`EspUsbHostMscFS`で`/usb`へマウントし、Arduino `fs::FS` / `File` APIでルートディレクトリを一覧したあと、小さな確認用ファイルを書き込み、読み戻して削除します。

このスケッチはUSBメモリへ書き込みます。書き込み・削除してよいUSBメモリを使ってください。

## 確認できること

- `mscWaitReady()`でMSCメディアの準備完了を待つ
- `mscGetBlockDeviceInfo()`でブロックデバイス情報を取得する
- `EspUsbHostMscFS::begin(usb, "/usb")`でFATをマウントし、`fs::FS`互換オブジェクトとして使う
- `File root = fs.open("/")`と`openNextFile()`でファイル一覧を読む
- `File::print()` / `File::readBytes()`でファイルを書き込み・読み戻しする
- `EspUsbHostMscFS::end()`でアンマウントする

`EspUsbHostMscFS`は`fs::FS`を継承しているため、`fs::FS &`を受け取るWebServerやUpdateなどのArduinoライブラリへ渡せます。現在のFatFs/VFSマウント経路はESP-IDF側のFatFs設定に依存し、32-bit sectorまでです。
