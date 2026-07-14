# Changelog / 変更履歴

## Unreleased

## 2.2.0
- (EN) Add `onKeyboardState()` with a format-independent 256-bit Keyboard/Keypad state for boot, Report-ID boot, and NKRO keyboards. Each changed report is delivered once with current and changed bitmaps, including modifier-only changes at usages `0xE0-0xE7`, plus `isDown()`, `wasPressed()`, and `wasReleased()` helpers. Existing `onKeyboard()` behavior is unchanged.
- (JA) boot、Report ID付きboot、NKROキーボード共通の256-bit Keyboard/Keypad状態を返す`onKeyboardState()`を追加。変化したreportごとに現在状態と変化bitmapを1回通知し、usage `0xE0～0xE7`の修飾キー単独変化も含みます。`isDown()`、`wasPressed()`、`wasReleased()`ヘルパーを備え、既存`onKeyboard()`の挙動は変更しません。

## 2.1.3
- (EN) Reduce static RAM usage for sketches that do not use USB networking by allocating the per-device 4,096-byte receive ring and 3,200-byte NTB reassembly buffer only when `networkOpen()` succeeds, then releasing them on `networkClose()` or device teardown. With the default eight device slots, this removes about 58 KB from the always-resident `EspUsbHost` instance; each open network interface now consumes about 7.3 KB dynamically instead.
- (JA) USBネットワークを使用しないスケッチの静的RAM使用量を削減。デバイスごとの4,096バイト受信リングと3,200バイトNTB再アセンブルバッファを、`networkOpen()`成功時のみ確保し、`networkClose()`またはデバイス切断時に解放するようにしました。既定の8デバイススロットでは、常駐する`EspUsbHost`インスタンスから約58 KBを削減し、ネットワーク使用時のみオープンしたインターフェースごとに約7.3 KBを動的に使用します。
- (EN) Further reduce static RAM by allocating HID input-field metadata only for devices whose report descriptors are parsed, and decoded HID event values only for endpoints that produce gamepad events. With the default eight device and sixteen endpoint slots, this removes another approximately 40 KB from the always-resident `EspUsbHost` instance while preserving basic HID and NKRO handling if an optional metadata allocation fails.
- (JA) HID入力フィールドのメタデータをreport descriptorを解析するデバイスだけに、デコード済みHIDイベント値をgamepadイベントを生成するエンドポイントだけに動的確保し、静的RAM使用量をさらに削減しました。既定の8デバイス・16エンドポイント構成では、常駐する`EspUsbHost`インスタンスからさらに約40 KBを削減します。任意のメタデータ確保に失敗した場合も、基本HID処理とNKRO処理は継続します。

## 2.1.2
- (EN) Fix ESP32-S2 builds that overflowed `dram0_0_seg`: `ESP_USB_HOST_MAX_DEVICES` (each slot is a multi-KB static `DeviceState`) now defaults to 3 on the RAM-constrained ESP32-S2 and stays 8 elsewhere, and can be overridden for any target with `-DESP_USB_HOST_MAX_DEVICES=N`. Added an `esp32s2` build profile to the examples (except the experimental `UsbNetwork`), a `tools/build_check.py` helper that compiles every example declaring a given sketch.yaml profile, and a GitHub Actions Build Check workflow that runs it for `esp32s3` and `esp32s2` on push/PR, so S2 memory regressions are caught automatically.
- (JA) `dram0_0_seg` オーバーフローで失敗していたESP32-S2ビルドを修正。1スロットが数KBの静的`DeviceState`である`ESP_USB_HOST_MAX_DEVICES`を、RAMの少ないESP32-S2では既定3・それ以外は8とし、`-DESP_USB_HOST_MAX_DEVICES=N`で任意ターゲットで上書き可能にしました。examples（実験的な`UsbNetwork`を除く）に`esp32s2`ビルドプロファイルを追加し、指定した sketch.yaml プロファイルで全 example をビルドする `tools/build_check.py` と、push/PR時に`esp32s3`・`esp32s2`で実行するGitHub Actions Build Checkワークフローを追加して、S2のメモリ回帰を自動検出できるようにしました。

## 2.1.1
- (EN) Add N-key rollover (NKRO) keyboard support on the host: the HID report descriptor is parsed to detect keyboards that report keys as a bitmap (report protocol) instead of the 6-key boot report, and they are decoded automatically so `onKeyboard()` delivers the same press/release events with no simultaneous-key limit. New diagnostic `keyboardUsesBitmapReport()`, `EspUsbHostKeyboardNKRO` example, and `hid_keyboard_nkro` peer test against `EspUsbDevice`.
- (JA) ホスト側でN-key rollover(NKRO)キーボードに対応。HID report descriptorを解析し、6キーのbootレポートではなくビットマップ(report protocol)でキーを送るキーボードを検出して自動デコードするため、同時押し数の制限なく`onKeyboard()`が同じpress/releaseイベントを返します。診断用`keyboardUsesBitmapReport()`、`EspUsbHostKeyboardNKRO`サンプル、`EspUsbDevice`との`hid_keyboard_nkro` peerテストを追加しました。
- (EN) Harden the experimental USB network (CDC-NCM) transmit path: reuse a per-device bulk-OUT transfer and completion semaphore instead of allocating them per frame, serialize concurrent senders (a user thread and the lwIP transmit hook) with a TX mutex, drain an in-flight send before teardown to avoid a use-after-free on disconnect, flush the endpoint on a send timeout instead of freeing a driver-owned transfer, resync NTB reassembly when a mid-block completion is lost, and apply `dns2` for static-IP configs.
- (JA) 実験的なUSBネットワーク(CDC-NCM)送信経路を堅牢化。フレームごとに転送とセマフォを確保せずper-deviceのbulk-OUT転送と完了セマフォを再利用し、同時送信(ユーザースレッドとlwIP transmit hook)をTX mutexで直列化し、切断時のuse-after-freeを避けるためteardown前に送信完了を待ち、送信タイムアウト時はドライバ保有中の転送を解放せずエンドポイントをフラッシュし、NTB再アセンブルを途中欠落時に再同期し、static IP設定で`dns2`を適用するようにしました。
- (EN) Document the USB Audio support scope in the README: UAC1 Type I streaming plus Feature Unit Mute/Volume are supported; UAC2, Clock Source/Selector, and Mixer/Selector/Processing units are not.
- (JA) READMEにUSB Audioの対応範囲を明記。UAC1 Type Iストリーミングと Feature UnitのMute/Volumeは対応、UAC2・Clock Source/Selector・Mixer/Selector/Processing unitは非対応であることを記載しました。

## 2.1.0
- (EN) Add generic non-HID vendor-specific bulk/control Host APIs: `vendorOpen()`, `vendorWrite()`, `vendorRead()`, `onVendorData()`, `vendorControlIn()`, and `vendorControlOut()`, with peer coverage for bulk echo, application vendor control requests, and WebUSB landing URL reads using `EspUsbDeviceVendor`.
- (JA) HIDではないvendor-specific interface向けの汎用Host APIとして、`vendorOpen()`、`vendorWrite()`、`vendorRead()`、`onVendorData()`、`vendorControlIn()`、`vendorControlOut()`を追加し、`EspUsbDeviceVendor`とのpeerテストでbulk echo、アプリ用vendor control request、WebUSB landing URL読み出しを確認するようにしました。
- (EN) Rename HID vendor-report APIs to make the distinction explicit: `onVendorInput()` / `sendVendorOutput()` / `sendVendorFeature()` are now `onHIDVendorInput()` / `sendHIDVendorOutput()` / `sendHIDVendorFeature()`.
- (JA) HID vendor report用APIであることを明確にするため、`onVendorInput()` / `sendVendorOutput()` / `sendVendorFeature()`を`onHIDVendorInput()` / `sendHIDVendorOutput()` / `sendHIDVendorFeature()`へリネームしました。
- (EN) Fix composite HID keyboard+mouse routing so Report ID based mouse reports on a keyboard-protocol HID interface are delivered to `onMouse()`.
- (JA) 複合HID keyboard+mouseで、keyboard protocolのHID interface上に届くReport ID付きmouse reportを`onMouse()`へ振り分けるよう修正しました。
- (EN) Fix boot keyboard reports with Shift modifiers being misdetected as Report ID based mouse reports when the interface does not actually define that Report ID.
- (JA) interfaceが該当Report IDを定義していない場合に、Shift modifier付きboot keyboard reportがReport ID付きmouse reportとして誤判定される問題を修正しました。
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
