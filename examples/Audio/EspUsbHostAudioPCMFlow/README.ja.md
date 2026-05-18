# EspUsbHostAudioPCMFlow

> English: [README.md](README.md)

[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)で整形済みPCMを生成し、USB Audio出力デバイスへ送信するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USBスピーカーなどのUSB Audio出力デバイス

## 動作内容

- スケッチ内で生成したWAVストリームを`ByteStream`としてPCMFlowへ渡す
- PCMFlowで48 kHz、stereo、16-bit PCMへ変換
- `onAudioOutputRequest()`で要求されたフレーム数を`audio.readFrames()`で取得
- 送信タイミングはEspUsbHost側に任せる

## 注意

- PCMFlowとの暫定連携サンプルです。PCMFlow側でデコード/変換し、EspUsbHost側がUSB Audio OUTの送信タイミングを管理します。
- 重要な連携点は`PCMFlow::readFrames(void *out, size_t frameCount)`です。

## 依存ライブラリ

- EspUsbHost
- PCMFlow
