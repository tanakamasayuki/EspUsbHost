# EspUsbHostAudioOutputTone

> English: [README.md](README.md)

左右で異なる周波数のステレオトーンを生成し、USBスピーカーなどのUSB Audio OUTデバイスへ送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 注意

- このサンプルは48 kHz、16-bit、ステレオ（2ch）PCMを送信します。
- 検出したUSB Audioストリームのフォーマット情報を表示し、対応しているOUTストリームが見つかった場合だけ送信を開始します。
- `onAudioOutputRequest()`で要求されたフレーム数だけPCMを生成し、送信タイミングはライブラリに任せます。
- 使用するデバイスに合わせて `isSupportedOutputStream()` 内の条件を調整してください。
- この基本トーン出力サンプルではハードウェアボリューム制御は使いません。Feature Unitのボリューム制御は`EspUsbHostAudioOutputHardwareVolume`を参照してください。

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
14. `loop()` が1秒ごとに `audioOutputRate()`、`audioOutputFeedbackRate()`、`audioOutputUnderruns()` を表示します。

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

## 押し込み型と引き出し型

`audioOutputStart()` はインターフェース切り替えの完了後、最低1 ms待ってから出力転送を投入します。USBコールバックから呼んだ場合は、そのコールバックを止めずにclient taskの後続処理で開始します。その場合の `true` は制御要求の投入成功を意味し、`audioOutputRunning()` は出力転送の開始後にtrueになります。

`audioSend()` はデータをコピーして転送を投入し、完了を待たずに返ります。`true` は投入成功で、デバイスへの到達保証ではありません。途中で `false` が返っても、それ以前のチャンクは投入済みの場合があるため、バッファ全体をそのまま再送すると重複し得ます。完了後の転送失敗は再送されません。

連続再生にはこのサンプルの `onAudioOutputRequest()` を使い、要求されたフレーム数だけ生成してください。`writtenFrames` が要求数より少ない場合、残りはライブラリが無音で埋めます。そのため受信バイト数は元のPCM量より多くなり得ます。無音補完があっても、バスやデバイスでの無欠落まで保証するものではありません。

## シリアル出力例

```
EspUsbHost Audio Output Tone example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1 proto=UAC1 clock=0 startable=1
audio output ready: addr=1
audio out: rate=48000 feedback=0 underruns=0
audio out: rate=48000 feedback=0 underruns=0
```

`feedback=0` は、そのデバイスがexplicit feedback endpointを持たず、ネゴシエート済み
レートで再生していることを示します。非同期デバイスは要求レートを報告してくるため、
`rate` がその値に追従します。

```
audio out: rate=48041 feedback=48041 underruns=0
```

## 関連

- [EspUsbHostAudioOutputHardwareVolume](../EspUsbHostAudioOutputHardwareVolume/) — USB Audio Feature Unitのmute/volume制御

## 読み方と確認の手順

### 実行前の準備

対応するUSB音声出力へPCMの試験音を生成する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`formatの対応を確認してからaudioOutputStart()`し、`onAudioOutputRequest()`で交互配置のsampleとwrittenFramesを用意します。音量より先にformatとstream開始を確認します。PCM振幅・機器の音量・feedback・underrunは別々に調べます。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostAudioOutputTone.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
