# Changelog / 変更履歴

## Unreleased
- (EN) Add generic non-HID vendor-specific bulk/control Host APIs: `vendorOpen()`, `vendorWrite()`, `vendorRead()`, `onVendorData()`, `vendorControlIn()`, and `vendorControlOut()`, with peer coverage using `EspUsbDeviceVendor`.
- (JA) HIDではないvendor-specific interface向けの汎用Host APIとして、`vendorOpen()`、`vendorWrite()`、`vendorRead()`、`onVendorData()`、`vendorControlIn()`、`vendorControlOut()`を追加し、`EspUsbDeviceVendor`とのpeerテストを追加しました。
- (EN) Rename HID vendor-report APIs to make the distinction explicit: `onVendorInput()` / `sendVendorOutput()` / `sendVendorFeature()` are now `onHIDVendorInput()` / `sendHIDVendorOutput()` / `sendHIDVendorFeature()`.
- (JA) HID vendor report用APIであることを明確にするため、`onVendorInput()` / `sendVendorOutput()` / `sendVendorFeature()`を`onHIDVendorInput()` / `sendHIDVendorOutput()` / `sendHIDVendorFeature()`へリネームしました。
- (EN) Fix composite HID keyboard+mouse routing so Report ID based mouse reports on a keyboard-protocol HID interface are delivered to `onMouse()`.
- (JA) 複合HID keyboard+mouseで、keyboard protocolのHID interface上に届くReport ID付きmouse reportを`onMouse()`へ振り分けるよう修正しました。
- (EN) Improve MSC compatibility with non-compliant devices such as DFMiniPlayer SD-card readers by avoiding BOT reset recovery after `SYNCHRONIZE CACHE(10)` failures, preventing EP0 STALL recovery loops before the existing skip-sync fallback can take effect.
- (JA) DFMiniPlayer内蔵SDカードリーダーなどの非準拠MSCデバイス向けに、`SYNCHRONIZE CACHE(10)` 失敗後のBOT reset recoveryを避け、既存のskip-syncフォールバック前にEP0 STALLの回復ループへ入ることを抑制しました。
- (EN) Document `usbMassStorage.setSkipSyncCache(true)` in the `EspUsbHostMSCFatList` example for devices that fail when FatFs requests `SYNCHRONIZE CACHE(10)`.
- (JA) FatFsが`SYNCHRONIZE CACHE(10)`を要求すると失敗するデバイス向けに、`EspUsbHostMSCFatList`サンプルへ`usbMassStorage.setSkipSyncCache(true)`の案内を追加しました。

## 2.0.1
- (EN) Fix HID boot mouse input routing so middle, back, and forward button reports are delivered to `onMouse()` instead of being misinterpreted as Report ID based HID reports.
- (JA) HID boot mouse入力の振り分けを修正し、middle/back/forwardボタンのレポートがReport ID付きHIDレポートとして誤判定されず、`onMouse()`に届くようにしました。

## 2.0.0
- (EN) Prepare the version 2 series as a ground-up redesign. Version 2 is not source-compatible with the 1.x API; sketches should migrate from inheritance/virtual overrides to the callback-based and class-specific APIs.
- (JA) バージョン2系に向けて全面的に再設計。2系は1系APIとソース互換ではありません。継承・仮想関数オーバーライド中心の使い方から、コールバック登録APIとUSBクラス別APIへ移行してください。
- (EN) Expand USB class support and examples, including HID keyboard/mouse/consumer/system/gamepad input, HID output helpers, CDC-ACM, vendor serial, USB Audio, Mass Storage, and Hub-oriented examples.
- (JA) HIDキーボード/マウス/コンシューマー/システム/ゲームパッド入力、HID出力ヘルパー、CDC-ACM、vendor serial、USB Audio、Mass Storage、Hub向けexamplesなど、USBクラス対応とサンプルを拡充。
- (EN) Add practical compatibility handling for non-compliant MSC devices, including fallback behavior around `SYNCHRONIZE CACHE(10)`.
- (JA) `SYNCHRONIZE CACHE(10)` 周辺のフォールバックを含む、非準拠MSCデバイス向けの実用的な互換性対応を追加。
- (EN) Document current v2 limitations and cautions: APIs may still change incompatibly during the 2.x series, real-device validation is still ongoing for USB Audio input, Hub edge cases, multi-device setups, unusual MSC devices, hot-plug behavior, and ESP32-P4 FS/HS OTG combinations.
- (JA) 現状のv2制限と注意点を明記。2系の間でも破壊的変更が入る可能性があり、USB Audio入力、Hubの細かい挙動、複数デバイス構成、特殊なMSCデバイス、活線挿抜、ESP32-P4 FS/HS OTG構成は実機検証を継続中です。

## 1.0.1
- (EN) Fix library.properties
- (JA) library.properties を修正

## 1.0.0
- (EN) Initial release
- (JA) 初期リリース
