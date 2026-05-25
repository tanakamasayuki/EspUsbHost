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
audio stream: addr=1 iface=1 alt=1 ep=0x01 dir=OUT channels=2 bytes=2 bits=16 rate=48000 rates=1 max_packet=196 interval=1
audio feature unit: unit=2 source=1 channels=2 master=0x3
audio mute off
audio volume range: -64.00..0.00 dB step 1.00 dB
audio hardware volume: -12.00 dB
audio output ready: addr=1
```
