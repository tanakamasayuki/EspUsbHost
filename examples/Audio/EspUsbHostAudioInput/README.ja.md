# EspUsbHostAudioInput

USBマイクなどのUSB Audio isochronous INデータを受信し、1秒ごとの受信バイト数を表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBマイクなどのUSB Audio入力デバイス

## 注意

- このサンプルは検出したUSB Audioストリームのフォーマット情報と、生のオーディオペイロードの受信量を表示します。
- スケッチ先頭の `PREFERRED_SAMPLE_RATE`、`PREFERRED_CHANNELS`、`PREFERRED_BITS_PER_SAMPLE` で使用する入力フォーマットを指定します。
- デフォルトでは `48000 Hz` に対応する最初の入力ストリームを選び、チャンネル数とbit数はデバイス側のstream設定を使います。
- 各設定を `0` にすると、条件にしません。すべて `0` にすると、最初の入力ストリームの設定を使います。
- 受信データのデコード、再生、保存は行いません。
- サンプルレート設定やFeature Unit制御が必要なデバイスは、追加対応が必要になる場合があります。

## 処理の流れ

1. `setup()` で `usb.onDeviceConnected()`、`usb.onDeviceDisconnected()`、`usb.onAudioData()` を登録します。
2. `usb.begin()` でUSB Hostを開始します。
3. USB Audioデバイスが接続されると `onDeviceConnected()` が呼ばれます。
4. `usb.audioInputReady(info.address)` で、そのデバイスにUSB Audio IN endpointが見つかっているか確認します。
5. `usb.getAudioStreams()` で、解析済みのAudio stream候補を取得します。
6. 各候補を `espUsbHostPrint(streams[i])` で表示します。
7. `matchesPreferredInput()` で、`input` 方向かつスケッチ先頭の希望条件に合う候補を探します。
8. 最初に一致した候補を `selectedStream` に保存します。
9. `usb.audioInputStart(selectedStream, selectedStream.sampleRate, info.address)` で、選択した入力streamを開始します。この呼び出しでサンプリングレート、interface、alternate setting、endpointがライブラリへ渡ります。
10. 音声データを受信すると `onAudioData()` が呼ばれます。
11. `onAudioData()` では、選択済みのデバイスアドレスとinterface番号に一致するデータだけを数えます。
12. 1秒ごとに、選択した `channels`、`bits`、`rate` と受信バイト数を表示します。

## `audioInputReady()` と `audioOutputReady()` について

この入力サンプルでは `onDeviceConnected()` の中で `audioInputReady()` を使っています。`audioInputReady()` はAudio IN endpointの検出確認であり、フォーマット選択や転送開始までは行いません。

現在のライブラリ実装では、`audioInputReady()` は対象デバイスにUSB Audio IN endpointが見つかっているかを確認します。ただしフォーマットはまだ選択されていません。そのため、このサンプルでは `getAudioStreams()` の結果から `streams[i].input` を見て、IN方向かつ希望フォーマットに一致するstreamを選び、`audioInputStart()` で開始しています。

出力側は `audioOutputReady()` です。こちらはAudio OUT endpointが見つかっているかを確認します。つまり対応関係は次のようになります。

| API | 主な意味 |
| --- | --- |
| `audioInputReady(address)` | USB Audio IN endpointが見つかっている |
| `audioInputStart(stream, sampleRate, address)` | 指定した入力streamを開始する |
| `audioOutputReady(address)` | USB Audio OUT endpointが見つかっている |
| `audioOutputStart(stream, sampleRate, address)` | 指定した出力streamを開始する |
| `streams[i].input` | そのstreamがUSBデバイスからESP32へ入ってくる入力データ |
| `streams[i].output` | そのstreamがESP32からUSBデバイスへ出ていく出力データ |

入力は `audioInputReady()` で検出し、`audioInputStart()` でフォーマットを指定して開始します。出力は `audioOutputReady()` で検出し、出力サンプル側で `audioOutputStart()` を呼びます。

## シリアル出力例

```
EspUsbHost Audio Input example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Microphone"
audio stream: addr=1 iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max_packet=96 interval=1
audio input selected: addr=1 iface=1 alt=1 channels=1 bytes=2 bits=16 rate=48000
audio input ready: addr=1
audio: addr=1 iface=1 channels=1 bits=16 rate=48000 bytes_per_sec=96000 last_chunk=96
```
