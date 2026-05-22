# EspUsbHostAudioOutputTone

左右で異なる周波数のステレオトーンを生成し、USBスピーカーなどのUSB Audio OUTデバイスへ送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 注意

- このサンプルは48 kHz、16-bit、ステレオ（2ch）PCMを送信します。
- 検出したUSB Audioストリームのフォーマット情報を表示し、対応しているOUTストリームが見つかった場合だけ送信を開始します。
- `onAudioOutputRequest()`で要求されたフレーム数だけPCMを生成し、送信タイミングはライブラリに任せます。
- 使用するデバイスに合わせて `isSupportedOutputStream()` 内の条件を調整してください。
- デバイスによってはFeature Unitやクロック制御の追加対応が必要になる場合があります。

## スケッチ先頭の設定

このサンプルでは、生成するトーンのPCMフォーマットをスケッチ先頭の定数で決めています。

| 定数 | 意味 | デフォルト |
| --- | --- | --- |
| `TONE_HZ_LEFT` | 左チャンネルのトーン周波数 | `440` |
| `TONE_HZ_RIGHT` | 右チャンネルのトーン周波数 | `880` |
| `VOLUME` | 16-bit PCMの振幅 | `100` |

受け入れるPCMフォーマットは `isSupportedOutputStream()` で定義しており、デフォルトでは 48 kHz / 2ch / 16-bit です。この条件に完全一致するUSB Audio OUT streamだけを選びます。

## 処理の流れ

1. `setup()` で `usb.onDeviceConnected()`、`usb.onDeviceDisconnected()`、`usb.onAudioOutputRequest()` を登録します。
2. `usb.begin()` でUSB Hostを開始します。
3. USB Audioデバイスが接続されると `onDeviceConnected()` が呼ばれます。
4. `usb.audioOutputReady(info.address)` で、そのデバイスにUSB Audio OUT endpointが見つかっているか確認します。
5. `usb.getAudioStreams()` で、解析済みのAudio stream候補を取得します。
6. 各候補を `espUsbHostPrint(streams[i])` で表示します。
7. `isSupportedOutputStream(sampleRate, channels, bitsPerSample)` で、受け入れるOUT stream形式を定義します。
8. `espUsbHostSelectAudioOutputStream()` が対応候補をスコアリングし、最適なstreamとサンプリングレートを選びます。
9. `usb.audioOutputStart(streams[selected.index], selected.sampleRate, info.address)` を呼び、出力streamを開始します。この呼び出しでサンプリングレート、interface、alternate setting、endpointがライブラリへ渡ります。
10. USB Audio OUT転送で次のPCMフレームが必要になると、`onAudioOutputRequest()` が呼ばれます。
11. `fillTone()` が `request.frameCount` の分だけPCMフレームを生成し、`request.data` へ書き込みます。
12. 生成したフレーム数を `request.writtenFrames` に設定します。
13. ライブラリが `request.data` の内容をUSB Audio OUT endpointへ送信します。

## `audioInputReady()` と `audioOutputReady()` について

入力側は `audioInputReady()` でUSB Audio IN endpointを検出し、`audioInputStart()` で選択した入力streamを開始します。

出力側は `audioOutputReady()` でUSB Audio OUT endpointを検出し、`audioOutputStart()` で選択した出力streamを開始します。このサンプルでは、トーン生成形式に一致した `streams[i]` を `audioOutputStart()` に渡しています。

| API | 主な意味 |
| --- | --- |
| `audioInputReady(address)` | USB Audio IN endpointが見つかっている |
| `audioInputStart(stream, sampleRate, address)` | 指定した入力streamを開始する |
| `audioOutputReady(address)` | USB Audio OUT endpointが見つかっている |
| `audioOutputStart(stream, sampleRate, address)` | 指定した出力streamを開始する |
| `streams[i].input` | そのstreamがUSBデバイスからESP32へ入ってくる入力データ |
| `streams[i].output` | そのstreamがESP32からUSBデバイスへ出ていく出力データ |

## シリアル出力例

```
EspUsbHost Audio Output Tone example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1
audio output ready: addr=1
```
