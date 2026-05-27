# EspUsbHostMSCFatList

USB Mass Storageデバイスを`EspUsbHostMscFS`で`/usb`へマウントし、Arduino `fs::FS` / `File` APIでルートディレクトリを一覧したあと、小さな確認用ファイルを書き込み、読み戻して削除します。

このスケッチはUSBメモリへ書き込みます。書き込み・削除してよいUSBメモリを使ってください。

## 確認できること

- `EspUsbHostMscFS::begin(usb, "/usb")`でFATをマウントし、`fs::FS`互換オブジェクトとして使う
- `EspUsbHostMscFS::mounted()`でマウント済みか確認する
- `File root = fs.open("/")`と`openNextFile()`でファイル一覧を読む
- `File::print()` / `File::readBytes()`でファイルを書き込み・読み戻しする

基本的な`fs::FS`利用では、スケッチ側で`mscReady()`、`mscWaitReady()`、`mscGetBlockDeviceInfo()`を直接呼ぶ必要はありません。MSCデバイスがまだ使えない間は`EspUsbHostMscFS::begin()`が失敗するため、少し待って再試行します。ブロックデバイスの詳細を見たい場合だけ、低レベルMSC APIを併用してください。

`EspUsbHostMscFS`は`fs::FS`を継承しているため、`fs::FS &`を受け取るWebServerやUpdateなどのArduinoライブラリへ渡せます。USBコールバック内からは呼ばず、`loop()`から使ってください。書き込み中やファイルを開いたままUSBメモリを抜いた場合、未反映データが失われる可能性があります。

現在のFatFs/VFSマウント経路はESP-IDF側のFatFs設定に依存し、32-bit sectorまでです。複数MSCデバイス同時接続はESP32-S3ではHCDチャネル数の制約が強いため、実用上は単一MSCデバイスを前提にしてください。
