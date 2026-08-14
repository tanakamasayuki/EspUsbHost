# EspUsbHost device lifecycle / MIDI listener API 仕様案

> **English readers:** the design proposal for the device lifecycle and MIDI listener API.
> **Adopted and implemented** — the shipped API is documented under "Device events" in
> [README.md](../README.md). This file is kept for the reasoning and the alternatives that
> were rejected.

> **状態: 採用・実装済み（Unreleased）。** 案 A を採用した。実装で判明した必須事項は
> 「実装メモ」に、仕様案の記述の誤りは該当箇所に反映済み。

## 目的

`EspUsbHost` の `onDeviceConnected()` / `onDeviceDisconnected()` / `onMidiMessage()` に、2.4.0 で入力系へ追加したものと同じ listener API を追加する。

この仕様は、統合ライブラリ `/home/mt/dev/ESP32KeyBridge/` からの要求を受けて定義する。ESP32KeyBridge は複数の adapter が同じ `EspUsbHost` stack を共有する構造で、その adapter 群が上記 3 つの単一 slot callback を奪い合うため、回避策として共有ハブを自前で持っている。

## 現状

2.4.0 でパース済み入力 6 種に固定容量・thread-safe な listener API を追加済み。

| event | listener API |
|---|---|
| `onKeyboard` | `addKeyboardListener()` |
| `onKeyboardState` | `addKeyboardStateListener()` |
| `onMouse` | `addMouseListener()` |
| `onConsumerControl` | `addConsumerControlListener()` |
| `onSystemControl` | `addSystemControlListener()` |
| `onGamepad` | `addGamepadListener()` |

一方、device lifecycle（`onDeviceConnected` / `onDeviceDisconnected`）と `onMidiMessage` は単一 slot のまま。`onHIDInput` / `onHIDReportDescriptor` / `onSerialData` / `onVendorData` / `onNetworkFrame` / `onAudioData` / `onAudioOutputRequest` / `onHIDVendorInput` も単一 slot だが、今回の対象外（後述）。

## 要求元の状況

ESP32KeyBridge の `src/ESP32KeyBridgeEspUsbHost.h` は、1 つの `EspUsbHost` stack につき 1 つの `EspUsbHostHub`（約 150 行）を持ち、単一 slot callback を代表して 1 回だけ登録し、複数の adapter へ手動でファンアウトしている。

ハブが購読しているフックは 6 つで、うち 4 つは 2.4.0 の listener で置き換え可能になっている。

| ハブが使うフック | listener の有無 | ハブが必要か |
|---|---|---|
| `onKeyboardState` | あり | 不要になった |
| `onConsumerControl` | あり | 不要になった |
| `onMouse` | あり | 不要になった |
| `onGamepad` | あり | 不要になった |
| `onMidiMessage` | **なし** | **必要** |
| `onDeviceDisconnected` | **なし** | **必要** |

つまり **6 分の 4 は既に回避策が不要なのに、残り 2 つのためだけにハブ全体が生き残っている**。

特に `onDeviceDisconnected` は、ハブ内で `MaxDisconnectSinks = 4` の配列へ手動登録して 4 つの adapter（keyboard / mouse / gamepad / MIDI）へ配るという、listener registry がやることそのものを再実装している。

ESP32KeyBridge 側の実害は 3 つ。

1. ハブ約 150 行と、それに付随する `portMUX` 操作の保守
2. adapter が stack ごとに 1 つのハブへ相乗りする必要があり、adapter 単独では自己完結しない
3. **スケッチが `onDeviceConnected` / `onDeviceDisconnected` / `onMidiMessage` を使えない**。ハブが所有しているため、スケッチが呼ぶと adapter が壊れる。ライブラリ側のドキュメントで「これらのフックは adapter が使うのでスケッチは触らないこと」と注意書きするしかない

## 提案 API

2.4.0 の listener API と同じ形にする。

```cpp
class EspUsbHost {
public:
  // Device lifecycle listeners. The parsed-input events already have these;
  // lifecycle is what an integration library still cannot share with the
  // sketch. Same contract as the input listeners: the single on*() callback
  // stays compatible and runs first, listeners run in registration order from
  // a per-event snapshot, removal is by id, and add/remove during a callback
  // takes effect on the next event.
  EspUsbHostListenerId addDeviceConnectedListener(DeviceCallback callback);
  EspUsbHostListenerId addDeviceDisconnectedListener(DeviceCallback callback);
  EspUsbHostListenerId addMidiMessageListener(MidiMessageCallback callback);
};
```

解除は既存の `removeListener(EspUsbHostListenerId)` をそのまま使う。ID 空間は既存 registry と共有する。

## 容量の検討（重要）

`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT` の既定は 4。

ESP32KeyBridge が `onDeviceDisconnected` を購読する adapter は keyboard / mouse / gamepad / MIDI の **4 つ**で、既定値をちょうど使い切る。スケッチが自分で 1 つ足した時点で溢れる。

入力系の listener は「1 つの event を複数の観測者が見る」用途で 4 で足りるが、**device lifecycle は全 adapter が購読する性質**なので、必要数が adapter 数に比例する。次のいずれかを選ぶ。

- **案 A**: device lifecycle だけ別の容量マクロを持つ（例 `ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS`、既定 8）。イベント特性の違いを型で表現でき、入力系の RAM を増やさずに済む
- **案 B**: `ESP_USB_HOST_MAX_LISTENERS_PER_EVENT` の既定を 8 へ引き上げる。単純だが、全 event の slot が倍になる
- **案 C**: 既定 4 のまま、溢れた場合は `ESP_USB_HOST_INVALID_LISTENER_ID` を返す既存挙動に委ね、ドキュメントで注意する

**案 A を採用した。** lifecycle と入力系は必要数の増え方が違うため、同じマクロで縛ると片方が必ず不適切になる。
RAM 差は根拠として弱い（slot は `id` 4 B + `shared_ptr` 8 B で、案 B の増分でも 6 event × 4 slot ≈ 400 B）ので、
判断の根拠は上記の増え方の違いそのものである。実装したマクロは `ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS`（既定 8）、
公開定数は `EspUsbHost::MaxLifecycleListeners`。

## 対象外とするもの

以下は単一 slot のままとする。

- `onAudioOutputRequest` — 「送出するデータを埋める」責務で、答えるのは 1 人であるべき。複数 listener を許すと誰の書き込みが採用されるのか曖昧になる（EspBle の `EspBleGattServer::onRead()` と同じ性質）
- `onHIDInput` / `onHIDReportDescriptor` / `onSerialData` / `onVendorData` / `onNetworkFrame` / `onAudioData` — 観測系なので将来 listener 化する余地はあるが、現時点で要求元がなく、ESP32KeyBridge も使っていない。必要になった時点で同じ形で追加する

「観測系は listener 化してよい / 応答系は単一 slot のまま」という切り分けを設計方針として残す。
この線は型で機械的に判定できる。`AudioOutputCallback` だけが `std::function<void(EspUsbHostAudioOutputRequest &)>` で
非 const 参照を取り、他のフックはすべて const 参照である。**非 const 参照 = 応答系 = 単一 slot、
const 参照 = 観測系 = listener 化可**。

## 実装メモ

仕様案の「2.4.0 と同じ形にする」は API 形状の話で、内部は入力系より作業が多い。実装で必須だった点。

1. 対象の 3 つは入力系と内部構造が違った。入力系は `std::shared_ptr<Callback>` + `hidCallbackMutex_` だが、
   `deviceConnectedCallback_` / `deviceDisconnectedCallback_` / `midiMessageCallback_` は素の `std::function` で
   setter も無同期の直接代入だった。同じ契約にするため 3 つを `shared_ptr` 化し `setHIDCallback()` 経由にした。
   private member のみの変更で API 互換。
2. `listenerIdInUseLocked()` は全 registry を列挙して ID 重複を防いでいる。新 registry 3 つの追加を忘れると
   ID が衝突し、`removeListener()` が別 event の listener を消す。`removeListener()` 本体への追加も同様。
3. `onDeviceConnected` の dispatch 箇所は 2 つあった（通常の enumeration と、列挙 event で取りこぼした
   device を拾うハブ address scan）。`dispatchDeviceConnected()` に集約した。
4. `handleMidi` は 4 byte packet ごとのループなので、snapshot はループ外で 1 回。先頭の
   `if (!midiMessageCallback_) return;` も listener の有無を見るよう変更した。
5. `ListenerRegistry` は `slots[]` にマクロを直書きしていたため、案 A には
   `template <typename Callback, size_t Capacity>` 化が必要で、4 つの template ヘルパと既存 6 registry の
   signature に波及した（private のみ、API 互換）。

## 効果

- ESP32KeyBridge の `EspUsbHostHub`（約 150 行）と `portMUX` 操作、および `MaxStacks = 2` の `forStack()` singleton 索引を削除でき、4 つの adapter（keyboard / mouse / gamepad / MIDI）がそれぞれ独立に `addXxxListener()` を呼ぶだけになる
- スケッチが `onDeviceConnected` / `onDeviceDisconnected` / `onMidiMessage` を自由に使えるようになり、「adapter が使うので触らないこと」という注意書きが不要になる
- 姉妹ライブラリの EspBle も同じ穴を持つ（GATT / HID Host event に listener があるのに接続 lifecycle だけ単一 slot）。`/home/mt/dev/EspBle/docs/PROPOSAL_KEYBRIDGE_ADAPTER.ja.md` の提案③がそれで、両者を揃えると 3 ライブラリの callback 契約が一貫する

## 検証

2.4.0 の入力 listener と同じ peer test 項目を、device lifecycle と MIDI に対して用意する。

- 単一 callback と listener の共存（callback が先、listener が登録順）
- listener だけを登録した場合の配送
- 容量超過時に `ESP_USB_HOST_INVALID_LISTENER_ID` を返すこと
- `removeListener()` による解除
- callback 実行中の add / remove が次 event から反映されること
- device lifecycle 固有: 接続 → 切断 → 再接続で全 listener へ届くこと。切断 event が listener 実行中の device 情報を有効に保つこと（`handleDeviceGone()` が `info` をローカルコピーしているため自動的に満たされる）
- 未サポート device の接続でも connect listener に届くこと（単一 callback は `info.supported` 判定より前に呼ばれており、listener もこの既存挙動に合わせる）
- ハブ address scan 経路からの connect 配送（上記実装メモ 3 の回帰）

実装は `tests/peer/usb_midi` に置いた。接続 event は DUT の `end()` + `begin()` による再列挙で、
切断 → 再接続は peer の再起動で作る。device 側の Arduino core に USB detach API がないため、
本物の切断を host に渡す手段は再起動しかない。

## 波及

- `src/EspUsbHost.h` / `src/EspUsbHost.cpp`
- `keywords.txt`、`CHANGELOG.md`
- `README.md` / `README.ja.md` の listener API 節（2.4.0 で追記した箇所へ lifecycle を追加）
- `tests/TEST_PLAN.md` / `TEST_PLAN.ja.md` の行追加、`tests/peer/README.md` / `README.ja.md`、`tests/conftest.py` の known リスト（peer 再起動で in-flight transfer が競合したときの 1 行）
- 採用後、ESP32KeyBridge 側で `EspUsbHostHub` を削除し、examples の `sketch.yaml` が指定する EspUsbHost バージョンを更新する
