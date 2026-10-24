# EspUsbHostAudioOutputMP3ESP8266Audio

> English: [README.md](README.md)

[ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)で埋め込みMP3ファイルをデコードし、USBスピーカーなどのUSB Audio出力デバイスで順番に再生するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 依存ライブラリ

- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio)（Arduino Library Managerで入手可能、GPL-3.0 License）— MP3デコード

## 動作内容

- MP3ファイルを`assets_embed.h`でCの配列として埋め込み、順番に再生
- `AudioGeneratorMP3`で各MP3をデコードし、`RingBufferOutput`経由でデコード済みPCMをリングバッファへ書き込む
- `RingBufferOutput::ConsumeSample()`内の線形補間リサンプラーで、MP3のソースレートから48 kHzへ変換
- リングバッファへ書き込むときに80%固定のソフトウェアゲインを適用
- `onAudioOutputRequest()`でバッファ済みPCMを`request.data`へコピーし、`request.writtenFrames`を設定
- USB Audio OUTの送信タイミングはEspUsbHostに任せ、デコードは`loop()`で進める

## 注意

- MP3ファイルは `assets_embed.h` でCの配列として埋め込まれており、順番に再生されます。MP3以外のファイルはスキップされます。
- デコーダが `SetRate()` でMP3のサンプルレートを通知します。線形補間リサンプラーによってUSB出力レート（48 kHz）に変換されます。
- USB出力は48 kHz・16-bit ステレオPCMです。`espUsbHostSelectAudioOutputStream()`はカスタムフィルターなしで呼び出し、ライブラリ側が最適なOUT streamを選択します。
- `onAudioOutputRequest()`で要求されたフレーム数をリングバッファから取り出し、送信タイミングはライブラリに任せます。
- 全ファイルを1周再生したら停止します。
- ここではUSBデバイス側のハードウェアボリューム制御は使いません。Feature Unitのボリューム制御は`EspUsbHostAudioOutputHardwareVolume`を参照してください。
- ESP8266AudioはMP3以外にも複数形式に対応する多機能ライブラリです。一方でGPL-3.0ライセンスのため、配布するプロジェクトで使う場合はライセンス条件を確認してください。
- ESP8266Audioは出力機能も持っていますが、このサンプルではUSB Audio OUTをEspUsbHostが管理するため、ESP8266Audioは主にコーデック部分だけを利用します。そのためリングバッファやリサンプリングなどの接続コードが必要です。
- 対応コーデックが多い分、PCMFlow版よりビルドに時間がかかることがあります。

## 主要API

- `usb.onAudioOutputRequest(callback)` — USB転送が次のPCMフレームを必要とするたびに呼ばれる
  - `request.data` — インターリーブされたPCMサンプルを書き込むバッファ
  - `request.frameCount` — 要求されたフレーム数
  - `request.writtenFrames` — 実際に書き込んだフレーム数を設定する
  - `request.channels` — 選択したストリームのチャンネル数
- `usb.audioOutputReady(address)` — USB Audio OUT endpointが見つかっているときtrue
- `usb.getAudioStreams(address, streams, maxStreams)` — 解析済みのAudio stream候補を返す
- `espUsbHostSelectAudioOutputStream(streams, count)` — 候補をスコアリングして最適なものを返す
- `usb.audioOutputStart(stream, sampleRate, address)` — 選択した出力streamを開始する
- `AudioFileSourcePROGMEM(data, size)` — 埋め込みMP3データをESP8266Audioのソースとしてラップ
- `AudioGeneratorMP3` / `mp3gen->begin(src, output)` / `mp3gen->loop()` — MP3デコードを駆動。`mp3gen->loop()`を`loop()`から呼ぶ
- `RingBufferOutput::SetRate(hz)` — デコーダがMP3ソースレートを通知するために呼ぶ。リサンプラーをリセット
- `RingBufferOutput::ConsumeSample(sample)` — ソースレートから48 kHzへの線形補間リサンプラー。リングバッファへ書き込む

## コーデックライブラリの選び方

このリポジトリには、MP3をUSB Audio OUTへ再生するサンプルが2つあります。

| サンプル | 向いている用途 |
| --- | --- |
| `EspUsbHostAudioOutputMP3PCMFlow` | MP3だけを再生したい場合。MITライセンスで軽量、USB Audio向けのPCM変換も扱いやすいです。 |
| `EspUsbHostAudioOutputMP3ESP8266Audio` | ESP8266Audioとの連携を確認したい場合。MP3以外にも複数形式へ対応する多機能ライブラリですが、GPL-3.0ライセンスに注意が必要です。 |

MP3だけであれば、まずPCMFlow版を使うのがシンプルです。このESP8266Audio版は、有名なESP8266Audioを既存プロジェクトで使っている場合や、ESP8266Audioのコーデック層との連携を確認したい場合に向いています。

## シリアル出力例

```
EspUsbHost Audio Output MP3 ESP8266Audio example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: device: address=1 portId=0x01 vid=xxxx pid=xxxx class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=192 interval=1 proto=UAC1 clock=0 startable=1
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## アセット

同梱の効果音は [OpenGameArt.org](https://opengameart.org) にて Juhani Junkala が公開している
[512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style) から使用しています。
ライセンスは [CC0 1.0 Universal（パブリックドメイン）](https://creativecommons.org/publicdomain/zero/1.0/) です。

## 読み方と確認の手順

### 実行前の準備

埋め込みMP3をデコードし、USB音声出力へ渡す。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

アセットheaderとcodec依存を含めて使います。`loop()`側のデコーダがPCM ringを埋め、USB出力callbackが準備済みframeを取り出します。まずtone例を試し、ファイル終端・ring不足・出力format非対応を分けて調べます。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostAudioOutputMP3ESP8266Audio.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
