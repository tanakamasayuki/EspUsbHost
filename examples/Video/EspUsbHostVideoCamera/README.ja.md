# EspUsbHostVideoCamera

> English: [README.md](README.md)

USB Video Class の API を確認します。カメラが申告する format/frame の組の読み取り、Probe/Commit の交渉、isochronous エンドポイントでのストリーミング、そして payload header から組み直した1フレームの受信までを行います。

## ハードウェア

- ESP32-P4 の OTG HS ポート — 既定プロファイルです。フルスピードのホストは約120バイトを超える isochronous IN パケットを受け取れず、カメラが要求する量にはるかに足りません
- **コンフィグレーションディスクリプタが256バイトに収まる** UVC カメラ。市販の webcam は収まりません。Logitech C920 は1,974バイトで、Arduino-ESP32 3.3.11 はホストスタックを `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` でビルドしているため、クラスドライバが動く前の列挙で失敗します。検証は [`tests/peer/usb_video/peer_device`](../../../tests/peer/usb_video/peer_device/)（兄弟ライブラリ `EspUsbDevice` で作ったカメラ）で行っています

いずれの制限も [docs/usb-host-advanced.ja.md](../../../docs/usb-host-advanced.ja.md#53-フルスピードポートの-isochronous-in) に実測値があり、スケッチ側では変更できません。

## 動作

- 接続時に、カメラが申告する format/frame の組をすべて表示します（`getVideoStreams`）
- その中で最大のフレームを選びます（`espUsbHostSelectVideoStream`）
- フォーマットの `dwMaxVideoFrameBufferSize` からフレームバッファを確保します
- Probe/Commit を交渉し、streaming alternate を選び、転送を投入します（`videoStart`）
- 各フレームをコールバックからコピーし、`loop()` で JPEG マーカーを確認します
- 1秒ごとに、フレームレートと「ストリームがデータを落としていないか」を示すカウンタを表示します

## 主なAPI

- `usb.getVideoStreamCount(address)` — このホストが読めるカメラとして列挙されたかどうか
- `usb.getVideoStreams(address, streams, max)` — Format / Frame の2階層ツリーを平坦化した、すべての組
- `espUsbHostSelectVideoStream(streams, count, format, width, height, fps)` — 1つ選ぶ。引数に `0` を渡すと「指定なし」
- `usb.videoStart(stream, fps, address)` — Probe/Commit、alternate 選択、転送投入。format・サイズ・レートを直接渡す形もあります
- `usb.onVideoFrame(callback)` — 組み直された1フレーム。USBタスクで呼ばれます
- `usb.videoCommitted(control, address)` — カメラが実際に commit した内容（要求どおりとは限りません）
- `usb.videoStats(stats, address)` — 失われた分。「注意」を参照
- `usb.videoStop(address)` — インタフェースを idle alternate に戻します。これがカメラに帯域予約をやめさせる方法です
- `espUsbHostVideoFrameIntervalToFps(interval)` / `espUsbHostVideoFormatName(format)` — UVC は100 ns単位で数え、フォーマットに番号を振ります

## 注意

- **フレームコールバックはUSBタスクで動き、そのタスクはストリーミング転送の再投入も行います。**転送4本×8パケットは、ハイスピードでは4 ms分のキューにすぎません。キューが空の瞬間に届いた isochronous パケットは失われ、再送はありません。コールバックで使った時間はストリームから引かれ、しかも**静かに**引かれます。stall も失敗パケットも出ず、フレームの中身が減るだけです。スループット測定の初期版はコールバック内で128 KB全体を検証しており、7.06 MB/s ではなく 3.47 MB/s、フレームは約6 KB短く届きました。このスケッチは `memcpy` を1回して戻ります
- 前のフレームを処理中に次が届いた場合は破棄してカウントします。破棄が問題になるなら、バッファのキュー化が次の一手です
- ESP32-P4 のハードウェアJPEGデコーダ（`driver/jpeg_decode.h`）も同じ理由で `loop()` 側に置きます。渡されるのは連続した1枚分のMJPEG画像で、デコーダの入力形式と一致します
- `videoStart()` はコールバックから呼べません。USBタスクが完了させるコントロール転送を待つためです。このスケッチではフラグを立てて `loop()` から開始します
- カメラが持たないフォーマット・サイズ・レートを指定した場合は、代替されずに**失敗します**。要求どおりのサイズで確保したフレームバッファに、それより大きいものが渡ることはありません:
  ```cpp
  usb.videoStart(ESP_USB_HOST_VIDEO_FORMAT_MJPEG, 640, 480, 30);
  ```
- isochronous 転送は再送されないため、`EspUsbHostVideoStats` がデータ欠落を知る唯一の手がかりです。良好な接続では `frames` と `payloads` 以外は0のままです。`packetErrors` はコントローラが失敗と報告したパケット（継続的に増えるなら、受け取れないパケットサイズを使っている）、`headerErrors` はデコードできなかった payload header、`payloadErrors` はカメラ自身が不良と申告した payload、`overflows` はバッファを超えたフレームです
- フレームバッファは、そのフォーマットについてカメラ自身が宣言した `dwMaxVideoFrameBufferSize` から確保します。MJPEG では `幅 × 高さ × バイト数` は誤りです

## シリアル出力例

```
EspUsbHost Video Camera example start
connected: device: address=1 portId=0x01 vid=303a pid=4028 class=0xef(Unknown) speed=high-speed product="EspUsbDevice UVC Camera"
video stream: addr=1 iface=1 ep=0x81 isoc MJPEG 320x240 format=1 frame=1 bpp=0 default=15fps rates=15-15fps step=0 max_frame=19200 payload=112 startable=1
video selected: addr=1 iface=1 format=MJPEG 320x240 fps=15 payload=112 frame_max=19200
video ready: addr=1
video: addr=1 fps=  0 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
frames carry no JPEG SOI/EOI markers (an uncompressed format, perhaps)
video: addr=1 fps=105 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
video: addr=1 fps=105 dropped=0 incomplete=0 packet_errors=0 header_errors=0 overflows=0
```

ESP32-P4 で `tests/peer/usb_video/peer_device` を相手に取得したものです。このうち3つの数値はホスト側ではなくカメラ側の事情によります。`payload=112` はそのカメラの `build_opt.h` がフルスピード向けに isochronous payload を固定しているため、`fps=105` は申告している15 fpsに合わせず連続してフレームを送っているため、JPEGマーカーが無いのはフレームが画像ではなくテストパターンであるためです。
