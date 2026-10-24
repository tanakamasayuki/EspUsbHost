# EspUsbHostAudioInput

> English: [README.md](README.md)

USBマイクなどのUSB Audio isochronous INデータを受信し、1秒ごとの受信バイト数とチャンネル別のピーク振幅・dBFSを表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBマイクなどのUSB Audio入力デバイス

## 注意

- このサンプルは検出したUSB Audioストリームのフォーマット情報、生のオーディオペイロードの受信量、およびチャンネル別のピーク振幅（int16 PCMの絶対値）とdBFSを表示します。
- ピーク振幅の計算は16bit PCM（`bytesPerSample == 2`）のときのみ行います。それ以外のフォーマットではバイト数のみ表示します。
- 最大 `MAX_PEAK_CHANNELS`（デフォルト 8）チャンネルまで対応し、1秒ごとに表示してリセットします。
- `isSupportedInputStream(sampleRate, channels, bitsPerSample)` で受け入れる入力フォーマットを指定します。
- デフォルトでは `48000 Hz` または `44100 Hz` に対応する入力ストリームを許可し、スコアリングで `48000 Hz`、16-bit、stereoを優先します。
- 受信データのデコード、再生、保存は行いません。
- サンプルレート設定やFeature Unit制御が必要なデバイスは、追加対応が必要になる場合があります。

## 処理の流れ

1. `setup()` で `usb.onDeviceConnected()`、`usb.onDeviceDisconnected()`、`usb.onAudioData()` を登録します。
2. `usb.begin()` でUSB Hostを開始します。
3. USB Audioデバイスが接続されると `onDeviceConnected()` が呼ばれます。
4. `usb.audioInputReady(info.address)` で、そのデバイスにUSB Audio IN endpointが見つかっているか確認します。
5. `usb.getAudioStreams()` で、解析済みのAudio stream候補を取得します。
6. 各候補を `espUsbHostPrint(streams[i])` で表示します。
7. `isSupportedInputStream(sampleRate, channels, bitsPerSample)` で、受け入れるIN stream形式を定義します。
8. `espUsbHostSelectAudioInputStream()` が対応候補をスコアリングし、最適なstreamとサンプリングレートを選びます。
9. 選択した候補を `selectedStream` に保存します。
10. `usb.audioInputStart(selectedStream, selected.sampleRate, info.address)` で、選択した入力streamを開始します。この呼び出しでサンプリングレート、interface、alternate setting、endpointがライブラリへ渡ります。
11. 音声データを受信すると `onAudioData()` が呼ばれます。
12. `onAudioData()` では、選択済みのデバイスアドレスとinterface番号に一致するデータだけを数えます。
13. 受信中に各チャンネルのピーク振幅（int16の絶対値）を更新します。
14. 1秒ごとに、選択した `channels`、`bits`、`rate`、受信バイト数、およびチャンネルごとの `ch{N}_peak` と dBFS を表示し、ピークをリセットします。

## `audioInputReady()` と `audioOutputReady()` について

この入力サンプルでは `onDeviceConnected()` の中で `audioInputReady()` を使っています。`audioInputReady()` はAudio IN endpointの検出確認であり、フォーマット選択や転送開始までは行いません。

現在のライブラリ実装では、`audioInputReady()` は対象デバイスにUSB Audio IN endpointが見つかっているかを確認します。ただしフォーマットはまだ選択されていません。そのため、このサンプルでは `getAudioStreams()` の結果から `streams[i].input` を見て、IN方向かつ希望フォーマットに一致するstreamを選び、`audioInputStart()` で開始しています。

出力側は `audioOutputReady()` です。こちらはAudio OUT endpointが見つかっているかを確認します。つまり対応関係は次のようになります。

| API | 主な意味 |
| --- | --- |
| `audioInputReady(address)` | USB Audio IN endpointが見つかっている |
| `audioInputStart(stream, sampleRate, address)` | 指定した入力streamを開始する |
| `audioInputStop(address)` | 受信転送とコールバックの完了を待ち、alternate 0へ戻す。成功ならtrue |
| `audioInputRunning(address)` | 入力が開始され、停止されていない |
| `audioOutputReady(address)` | USB Audio OUT endpointが見つかっている |
| `audioOutputStart(stream, sampleRate, address)` | 指定した出力streamを開始する |
| `streams[i].input` | そのstreamがUSBデバイスからESP32へ入ってくる入力データ |
| `streams[i].output` | そのstreamがESP32からUSBデバイスへ出ていく出力データ |

入力は `audioInputReady()` で検出し、`audioInputStart()` でフォーマットを指定して開始します。出力は `audioOutputReady()` で検出し、出力サンプル側で `audioOutputStart()` を呼びます。

## 停止・再開と受信レート

入力は最大4本の転送（各8パケット）をキューに保持し、再投入までの空白を減らします。`audioInputStop()` はUSB client taskの完了処理を待つため、`loop()` などから呼び、USBコールバック内からは呼ばないでください。停止後は同じstreamを `audioInputStart()` で再開できます。`audioInputStart()` は接続コールバック内からも呼べますが、その場合は制御要求の投入成功を返します。

受信コールバックで時間を使うと、キューを使い切ってアイソクロナスの取りこぼしが発生します。サンプルレートの設定値と受信量は別に記録してください。32 kHz・16 bit・monoの期待値は64,000 B/sですが、連番の連続性だけを確認しても毎フレーム受信できたことの証明にはなりません。

## シリアル出力例

```
EspUsbHost Audio Input example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Microphone"
audio stream: addr=1 iface=1 alt=1 ep=0x81 dir=IN channels=1 bytes=2 bits=16 rate=48000 rates=1 max_packet=96 interval=1 proto=UAC1 clock=0 startable=1
audio input selected: addr=1 iface=1 alt=1 channels=1 bytes=2 bits=16 rate=48000
audio input ready: addr=1
audio: addr=1 iface=1 channels=1 bits=16 rate=48000 bytes_per_sec=96000 ch0_peak=12345( -8.5dBFS)
```

## 読み方と確認の手順

### 実行前の準備

USBマイクのPCMを受け、信号のピークを調べる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

入力streamを列挙して対応formatを選び、開始後のonAudioData()を追います。声や軽い音でピークとbyte数を比較します。無音でもbyte数が増えるのは正常な場合があるため、選んだchannel数とsample幅に合わせて解釈します。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostAudioInput.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
