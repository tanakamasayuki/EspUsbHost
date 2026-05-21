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
- PCMFlowはMITライセンスの軽量なMP3デコード/PCM変換ライブラリです。MP3再生だけが目的なら、このサンプルを基本の選択肢として推奨します。

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

## アセット

同梱の効果音は [OpenGameArt.org](https://opengameart.org) にて Juhani Junkala が公開している
[512 Sound Effects (8-bit style)](https://opengameart.org/content/512-sound-effects-8-bit-style) から使用しています。
ライセンスは [CC0 1.0 Universal（パブリックドメイン）](https://creativecommons.org/publicdomain/zero/1.0/) です。
