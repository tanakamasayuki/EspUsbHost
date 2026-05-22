# EspUsbHostAudioOutputMP3PCMFlow

> English: [README.md](README.md)

[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)で埋め込みMP3アセットをデコードし、整形済みPCMをUSB Audio出力デバイスへ送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 動作内容

- このスケッチ内の埋め込みMP3アセットを使用
- `audio.open(data, size, PCMFlow::CodecKind::Mp3)`で各MP3を開く
- 接続されたUSB AudioデバイスからOUTストリームを選択
- モノラルスピーカーならmonoなど、選択したUSB Audioフォーマットに合わせてPCMFlowを設定
- `onAudioOutputRequest()`で要求されたフレーム数を`audio.readFrames()`で取得
- 送信タイミングはEspUsbHost側に任せる

## 注意

- PCMFlow側でデコード/変換し、EspUsbHost側がUSB Audio OUTの送信タイミングを管理します。
- このサンプルは、1chまたは2ch、8-bitまたは16-bit PCMとして報告されるUSB Audio OUTストリームに対応します。
- 重要な連携点は`PCMFlow::readFrames(void *out, size_t frameCount)`です。
- デコードは`audio.pump()`で`loop()`から進め、USBコールバック内ではデコード済みPCMフレームのコピーだけを行います。
- PCMFlowはMITライセンスの軽量なMP3デコード/PCM変換ライブラリです。MP3再生だけが目的なら、このサンプルを基本の選択肢として推奨します。

## 処理の流れ

1. `setup()`で`usb.onDeviceConnected()`、`usb.onDeviceDisconnected()`、`usb.onAudioOutputRequest()`を登録します。
2. `usb.begin()`でUSB Hostを開始します。
3. `openNextFile()`で最初の埋め込みMP3を`audio.open()`で開きます。
4. USB Audioデバイスが接続されると`onDeviceConnected()`が呼ばれます。
5. `usb.audioOutputReady(info.address)`で、そのデバイスにUSB Audio OUT endpointが見つかっているか確認します。
6. `usb.getAudioStreams()`で解析済みのAudio stream候補を取得し、`espUsbHostPrint()`で各候補を表示します。
7. `isSupportedOutputStream()`で受け入れるOUT streamフォーマットを定義します。
8. `espUsbHostSelectAudioOutputStream()`が対応候補をスコアリングし、最適なstreamとサンプリングレートを選びます。
9. `audio.setOutputFormat(outputFormat)`でPCMFlowの出力形式を選択したUSBストリーム（サンプルレート、チャンネル数、ビット深度）に合わせます。
10. `restartCurrentFile()`で、決定したフォーマットで現在のMP3を開き直します。
11. `usb.audioOutputStart(stream, sampleRate, address)`を呼び、出力streamを開始します。
12. USB Audio OUTの転送でPCMフレームが必要になると`onAudioOutputRequest()`が呼ばれます。
13. `audio.readFrames(request.data, request.frameCount)`でデコード済みフレームをコピーし、書き込んだフレーム数を返します。
14. スケッチはその数を`request.writtenFrames`に設定します。
15. `loop()`は`audio.pump()`でデコードを進め、`audio.isEof()`がtrueになったら`openNextFile()`で次のファイルへ進みます。

## 主要API

- `usb.onAudioOutputRequest(callback)` — USB転送が次のPCMフレームを必要とするたびに呼ばれる
  - `request.data` — インターリーブされたPCMサンプルを書き込むバッファ
  - `request.frameCount` — 要求されたフレーム数
  - `request.writtenFrames` — 実際に書き込んだフレーム数を設定する
  - `request.channels` — 選択したストリームのチャンネル数
- `usb.audioOutputReady(address)` — USB Audio OUT endpointが見つかっているときtrue
- `usb.getAudioStreams(address, streams, maxStreams)` — 解析済みのAudio stream候補を返す
- `espUsbHostSelectAudioOutputStream(streams, count, filter)` — 候補をスコアリングして最適なものを返す
- `usb.audioOutputStart(stream, sampleRate, address)` — 選択した出力streamを開始する
- `audio.open(data, size, PCMFlow::CodecKind::Mp3)` — 埋め込みMP3をデコード用に開く
- `audio.setOutputFormat(format)` — 出力のサンプルレート・チャンネル数・ビット深度を設定
- `audio.setGain(gain)` — 音量を設定（0.0〜1.0）
- `audio.readFrames(out, frameCount)` — デコード済みフレームをコピーし、書き込んだフレーム数を返す
- `audio.pump()` — デコードを進める。`loop()`から呼ぶ
- `audio.isEof()` — 現在のファイルが終端に達したときtrue

## 依存ライブラリ

- EspUsbHost
- [PCMFlow](https://github.com/tanakamasayuki/PCMFlow)（MIT License）— MP3デコード/PCM変換

## コーデックライブラリの選び方

このリポジトリには、MP3をUSB Audio OUTへ再生するサンプルが2つあります。

| サンプル | 向いている用途 |
| --- | --- |
| `EspUsbHostAudioOutputMP3PCMFlow` | MP3だけを再生したい場合。MITライセンスで軽量、USB Audio向けのPCM変換も扱いやすいです。 |
| `EspUsbHostAudioOutputMP3ESP8266Audio` | ESP8266Audioとの連携を確認したい場合。MP3以外にも複数形式へ対応する多機能ライブラリですが、GPL-3.0ライセンスに注意が必要です。 |

MP3だけであれば、まずPCMFlow版を使うのがシンプルです。ESP8266Audio版は有名で対応形式が多い一方、USB Audioでは出力機能ではなくコーデック部分を主に使うため、リングバッファやリサンプリングなどの接続コードがやや増えます。また対応コーデックが多い分、ビルドに時間がかかることがあります。

## シリアル出力例

```
EspUsbHost Audio Output MP3 PCMFlow example start
playing: /sfx_alarm_loop6.mp3 (5447 bytes)
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x03 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=192 interval=1
selected PCMFlow output: 48000 Hz, 2 ch, 16-bit
audio output ready: addr=1
playing: /sfx_coin_double1.mp3 (4265 bytes)
...
all files played
```

## アセット

同梱の効果音は [OpenGameArt.org](https://opengameart.org) にて Juhani Junkala が公開している
[512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style) から使用しています。
ライセンスは [CC0 1.0 Universal（パブリックドメイン）](https://creativecommons.org/publicdomain/zero/1.0/) です。
