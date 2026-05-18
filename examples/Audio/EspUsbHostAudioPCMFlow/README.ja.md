# EspUsbHostAudioPCMFlow

> English: [README.md](README.md)

[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)で埋め込みMP3アセットをデコードし、整形済みPCMをUSB Audio出力デバイスへ送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 動作内容

- このスケッチ内の埋め込みMP3アセットを使用
- `audio.open(data, size, PCMFlow::CodecKind::Mp3)`で各MP3を開く
- PCMFlowで48 kHz、stereo、16-bit PCMへ変換
- `onAudioOutputRequest()`で要求されたフレーム数を`audio.readFrames()`で取得
- 送信タイミングはEspUsbHost側に任せる

## 注意

- PCMFlow側でデコード/変換し、EspUsbHost側がUSB Audio OUTの送信タイミングを管理します。
- 重要な連携点は`PCMFlow::readFrames(void *out, size_t frameCount)`です。

## 依存ライブラリ

- EspUsbHost
- PCMFlow
