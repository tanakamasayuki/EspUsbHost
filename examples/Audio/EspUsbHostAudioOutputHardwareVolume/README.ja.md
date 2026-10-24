# EspUsbHostAudioOutputHardwareVolume

> English: [README.md](README.md)

USB Audio OUTの簡単なトーンを出しながら、USBデバイス側のハードウェアmute/volume Feature Unitを操作するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- ハードウェアmuteまたはvolumeに対応したUSB Audio出力デバイス

## 注意

- 多くのUSBスピーカーや小型USBオーディオアダプタは、利用可能なハードウェアボリューム制御を公開していません。
- このサンプルはUSB Audio ClassのFeature Unit対応を確認するためのものです。通常の再生は`EspUsbHostAudioOutputTone`または`EspUsbHostAudioOutputMP3PCMFlow`から始めてください。
- トーン出力は48 kHz、16-bit、ステレオPCMです。
- 検出したFeature Unitを表示し、muteがあれば解除し、volumeを`USB_OUTPUT_VOLUME_DB`へクランプ付きで設定します。

## 主要API

- `usb.getAudioFeatureUnits(address, units, maxUnits)` — 解析済みUSB Audio Feature Unitを取得
- `usb.audioHasMute(address, unitId)` / `usb.audioSetMute(mute, address, unitId)`
- `usb.audioHasVolume(address, unitId)`
- `usb.audioGetVolumeRange(range, address, unitId)`
- `usb.audioSetVolumeDbClamped(db, address, unitId)`
- `usb.audioGetVolumeDb(db, address, unitId)`

## シリアル出力例

```
EspUsbHost Audio Output Hardware Volume example start
connected: device: address=1 portId=0x01 vid=1234 pid=5678 class=0x00(Device) speed=full product="USB Speaker"
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1 proto=UAC1 clock=0 startable=1
audio feature unit: unit=2 source=1 channels=2 master=0x3
audio mute off
audio volume range: -64.00..0.00 dB step 1.00 dB
audio hardware volume: -12.00 dB
audio output ready: addr=1
```

## 読み方と確認の手順

### 実行前の準備

生成PCMの振幅とUSB機器側の音量制御を区別する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

出力stream選択後にFeature Unitと対応controlを調べてからmute・volumeを設定します。要求dB、読み戻し値、範囲を比べます。対象のFeature Unitがないことと、音声stream自体の失敗は別です。

### APIの使い方と改造時の注意

USB音声の方向はdeviceとhostの関係から読みます。PCMを解釈する前に方向・sample rate・channel数・sampleあたりbyte数・有効bit数を合わせます。frameは全channel分のsample一組であり、byte数とは違います。stereoのinterleaved配置はchannelを交互に並べます。

streamの有効化にはホストの操作が必要です。USB列挙の成功と、stream状態・不足・あふれの統計を分けて見ます。bufferとqueueは有限で、`write()`の返却長を無視する例が無欠落を保証するわけではありません。USB callbackを短くし、デコード・保存・重い解析は別へ置きます。codecや外部回路を足す前に既知の試験信号で確かめます。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostAudioOutputHardwareVolume.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
