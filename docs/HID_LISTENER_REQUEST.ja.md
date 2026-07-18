# 提案: HID入力callbackへのlistener（複数購読）追加

- 発信元: EspBle（姉妹BLEライブラリ）
- 対象: EspUsbHostの入力callback API（`onKeyboard`ほか）
- 種別: API追加提案（後方互換。既存APIは変更しない）

## 背景・動機

EspUsbHostの入力callbackは種別ごとに**単一**です（`onKeyboard(cb)`, `onMouse(cb)` …）。単一slotのため、後から登録すると前のcallbackを上書きします。

これが問題になるのは、**アダプタ層とスケッチが同じイベントを同時に購読したい**ケースです。具体的には ESP32KeyBridge のような入力アダプタが`onKeyboard`を使うと、スケッチ側は同じ`onKeyboard`を使えず（上書きされる）、アダプタとスケッチのcallbackが共存できません。

EspBleでは同じ課題に対し、単一の`on*()`に加えて**event種別ごとに複数登録できるlistener API**を実装し、アダプタとスケッチのcallback共存をPeerテストで確認済みです。EspUsbHostでも同じ用途（KeyBridgeアダプタ＋スケッチの併用）が見込まれるため、同様の追加を提案します。

## 提案するAPI

既存の単一`on<種別>()`は**そのまま維持**し（後方互換）、各入力callbackに対応するlistener登録・解除を追加します。命名・id型はEspUsbHostの慣習（`ESP_USB_HOST_*` / `EspUsbHost*`）に合わせてください。EspBleでの対応例を併記します。

| 既存（単一） | 追加（複数購読） | EspBleの対応API |
|---|---|---|
| `onKeyboard(cb)` | `addKeyboardListener(cb)` / `removeListener(id)` | `addKeyboardListener` / `removeListener` |
| `onKeyboardState(cb)` | `addKeyboardStateListener(cb)` | `addKeyboardStateListener` |
| `onMouse(cb)` | `addMouseListener(cb)` | （EspBleは今後追加予定） |
| `onConsumerControl(cb)` | `addConsumerControlListener(cb)` | 同上 |
| `onSystemControl(cb)` | `addSystemControlListener(cb)` | 同上 |
| `onGamepad(cb)` | `addGamepadListener(cb)` | 同上 |

同じ方式は`onDeviceConnected`/`onDeviceDisconnected`など他のcallbackへも一般化できますが、まずHID入力callback群を主対象とします。

## 動作仕様（EspBleでの確定仕様）

EspBleのlistenerは次の仕様です。EspUsbHostでも同等の挙動を推奨します。

- 追加は`EspBleListenerId`（0以外の値）を返す。`0`（`EspBleInvalidListenerId`）は無効値。
- event種別ごとの最大登録数は固定（EspBleは`MaxListenersPerEvent = 4`）。
- 空callback・容量超過・存在しないidの解除は明示的に失敗（`false` / 無効id）を返す。
- 配送開始時にlistener集合をsnapshotし、callback内での追加・解除は**次のeventから**反映（配送中の集合変更で不整合を起こさない）。
- 単一`on<種別>()`とlistenerは同じ配送contextで、`on<種別>()`→listener登録順に呼ぶ。

### EspBleの参考signature

```cpp
using EspBleListenerId = uint32_t;
constexpr EspBleListenerId EspBleInvalidListenerId = 0;

EspBleListenerId addKeyboardListener(KeyboardCallback callback);   // 0以外=成功
bool             removeListener(EspBleListenerId listenerId);       // true=解除成功
static constexpr size_t MaxListenersPerEvent = 4;
```

EspUsbHostは背景taskからcallbackを配送するため、listener集合へのアクセスは配送task/登録スレッド間で保護が必要です（EspBleはmutexで保護）。

## 互換性

- 既存の`on<種別>()`は無変更。既存スケッチは影響を受けません。
- listenerは純粋な追加API。採用しないスケッチはこれまでどおり単一callbackを使えます。

## 検証状況（EspBle側）

EspBleの`tests/peer`でアダプタlistenerとスケッチcallbackの共存、登録・解除、容量超過・無効idの失敗を確認済みです。EspUsbHostへ移植する際は同等のケースをEspUsbHostのテスト方式で確認することを推奨します。

## 備考

これは一方的な変更依頼ではなく、方針のすり合わせを前提とした提案です。EspUsbHost側の設計事情（背景taskのcontext、既存APIとの整合、命名）に合わせて調整してください。不要・時期尚早と判断される場合はこの文書を破棄して構いません。
