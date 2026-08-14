# EspUsbHost MIDI cable 情報の公開 仕様案

> **English readers:** the design proposal for exposing USB MIDI cable (virtual port)
> information. The cable **count** is implemented (`getMidiPortInfo()`, see "MIDI" in
> [README.md](../README.md)); cable **names** are not. Kept for the reasoning.

> **状態: cable 数は実装済み、cable 名は未着手。** 統合ライブラリ
> `/home/mt/dev/EspMidi/` からの要求。既存 API を変えない純粋な追加として設計している。
>
> 実装にあたって提案から変えた点と、実装時に判明した仕様上の注意は
> [実装結果](#実装結果)にまとめた。

## 目的

接続された USB MIDI 機器が **いくつの cable(仮想 MIDI ポート)を持つか** を、接続時点で問い合わせられるようにする。あわせて cable ごとの jack 名を取得できるようにする。

## 現状

MIDI インターフェースの発見は行っているが、**cable 情報は一切パースしていない**。`DeviceState` が持つ MIDI 関連フィールドは次の 5 つだけである(`src/EspUsbHost.h:2194-2198`)。

```cpp
bool     hasMidiInterface   = false;
uint8_t  midiInterfaceNumber = 0;
bool     hasMidiOutEndpoint = false;
uint8_t  midiOutEndpointAddress = 0;
uint16_t midiOutPacketSize  = 0;
```

`EspUsbHostDeviceInfo`(`src/EspUsbHost.h:212`)にも MIDI に関する項目はない。

cable 番号は `EspUsbHostMidiMessage::cable` として**受信したメッセージから事後的に**分かるだけで、機器が何本持っているかは分からない。`midiReady(address)` は真偽しか返さない。

descriptor 側には情報がある。MS インターフェースのバルクエンドポイント記述子の直後に続く CS_ENDPOINT(`MIDI_CS_ENDPOINT_GENERAL`)が `bNumEmbMIDIJack` と埋め込み jack ID の配列を持ち、IN/OUT Jack 記述子が `iJack` に名前の string index を持つ。現在の走査はここを読み飛ばしている。

パース箇所の候補は既存の走査と同じ位置になる。

- インターフェース走査: `src/EspUsbHost.cpp:7958`(`hasMidiInterface = true` を立てている箇所)。ここで IN/OUT Jack 記述子から `iJack` を拾える。
- エンドポイント走査: `src/EspUsbHost.cpp:8127-8129`(`hasMidiOutEndpoint` / `midiOutPacketSize` を立てている箇所)。この直後の CS_ENDPOINT が cable 数を持つ。

## 提案 API

```cpp
struct EspUsbHostMidiPortInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t inCableCount  = 0;  // 機器 → ホスト方向の cable 数
  uint8_t outCableCount = 0;  // ホスト → 機器方向の cable 数
};

class EspUsbHost {
public:
  // 接続中の MIDI 機器の cable 構成を取得する。MIDI インターフェースを
  // 持たない、あるいは未接続なら false。
  bool getMidiPortInfo(uint8_t address, EspUsbHostMidiPortInfo &info) const;

  // cable の名前(jack の iJack 文字列)。無名なら空文字列。
  // cableNumber は 0 起算(EspUsbHostMidiMessage::cable と同じ基準)。
  const char *midiCableName(uint8_t address, uint8_t cableNumber, bool input) const;
};
```

命名と粒度は既存の `getDevice(address, EspUsbHostDeviceInfo &)` に合わせている。

cable 数だけ先に入れば EspMidi 側は動くので、`midiCableName()` は後追いでもよい。

## 要求元の状況

`EspMidi` は内部モデルを 2 階層にしている。

```text
Endpoint(接続の単位。切断はここで起きる)
 └─ Port(最大 16。USB では cable、MIDI 2.0 では group に対応)
```

さらに **ポートのハンドルを「論理的な席」として扱う**方針を採っている。機器が切断されても席は残り、ルーティング設定は保持され、同じ機器が再接続したら識別子(VID / PID / serial)照合で同じ席へ戻る。これによりアプリケーションは抜き差しのたびにルーティングを再設定しなくて済む。

この方式は **接続した時点でポート数が分かること**を前提にしている。現状は「トラフィックが届いた cable 番号から後追いで知る」しかないため、次の問題が起きる。

1. **無音のポートが存在しないことになる。** 送信専用の cable や、まだ何も弾いていない cable は席が作られない。
2. **席が後から増える。** ルーティング設定の対象が実行中に湧いてくるので、起動時にルーティングを組み切れない。
3. **出力ポート数が全く分からない。** ホスト → 機器方向は受信メッセージが存在しないため、cable 番号を推測する手段がない。`midiSend()` は生バイト送信なので EspMidi 側で header に cable を立てられるが、**何本まで有効かを知る方法がない**。

3 番目が特に問題で、複数ポートを持つ MIDI 音源へ EspMidi から振り分ける構成が組めない。

## 不足していないと確認できたもの

参考として、EspMidi が必要とする他の情報は既存 API で足りている。

| 必要なもの | 既存 API |
| --- | --- |
| 接続 / 切断イベント | `addDeviceConnectedListener()` / `addDeviceDisconnectedListener()` |
| 機器識別(席の再照合用) | `EspUsbHostDeviceInfo` の `vid` / `pid` / `serial` / `product` |
| 複数 MIDI 機器の識別 | 各 API の `address` 引数 |
| cable 指定送信 | `midiSend()` が生バイト送信なので header に cable を立てられる |
| MIDI 準備完了 | `midiReady(address)`(EspMidi は `update()` 駆動なのでポーリングで足りる) |
| 受信 | `addMidiMessageListener()` |

## テスト

`tests/peer/usb_midi` の Device 役スケッチを複数 cable の MIDI インターフェースにして(EspUsbDevice 側の複数 cable 対応が前提)、Host 役が `getMidiPortInfo()` で本数を正しく取得できることを確認する。

EspUsbDevice 側にも対応する仕様案 `EspUsbDevice/docs/MIDI_MULTI_CABLE_PROPOSAL.ja.md` を出している。両方入ると peer テストで cable を跨いだ往復が確認できる。

## 実装結果

cable 数のみ実装した。`midiCableName()` は未実装で、後述の理由から別途扱う。

### 提案から変えた点

**シグネチャ。** `getMidiPortInfo(uint8_t address, EspUsbHostMidiPortInfo &info)` ではなく
`getMidiPortInfo(EspUsbHostMidiPortInfo &info, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
とした。`getDevice()` に合わせるより、`midiReady()` をはじめ MIDI API 全体が
末尾に既定値付きの `address` を取る規約に合わせるほうを優先している。

**パース位置。** 提案が挙げていた `src/EspUsbHost.cpp:7958`(インターフェース記述子)ではなく、
`handleDescriptor()` に新設した `USB_CS_ENDPOINT_DESC` の case で読む。cable 数を持つのは
CS_ENDPOINT であって、インターフェース記述子でも `hasMidiOutEndpoint` を立てる箇所でもない。
CS_ENDPOINT は走査に case 自体が無かったので追加した。CS_ENDPOINT はオーディオの
アイソクロナス endpoint にも付くため、直前に通過した MS bulk endpoint の方向を
`currentMidiEndpointDirection_` に latch し、endpoint 記述子ごとに必ずクリアしている。

### jack 名と endpoint 方向は逆

クラス仕様は embedded jack を**機器側から見た**名前で呼ぶ。bulk **IN** endpoint に付く
CS_ENDPOINT が列挙するのは Embedded MIDI **OUT** Jack、bulk OUT endpoint 側が
Embedded MIDI **IN** Jack である。`inCableCount` / `outCableCount` は MIDI API の他の部分と
同じくホストから見た方向にしたので、descriptor を読む側とは名前が反転する。

このため `midiCableName(address, cable, bool input)` の `input` は意味が二通りに読める。
実装するときは `bool` をやめ、`midiInCableName()` / `midiOutCableName()` の 2 関数に
分けるべきである。

### cable 名を分けた理由

ESP-IDF の USB Host は文字列を manufacturer / product / serial の 3 本しかキャッシュしない
(`usbString(devInfo.str_desc_*)`)。任意の string index を取る API は無いので、`iJack` には
GET_DESCRIPTOR(STRING) の制御転送を client task 上で非同期に自前実装する必要がある
(UAC2 の Clock Source と同じ枠組みの前例はある)。加えて jack ID → `iJack` の対応表と
CS_ENDPOINT の `baAssocJackID` 配列を機器ごとに保持することになる。

対して cable 数は descriptor のパースのみで I/O が無く、`DeviceState` に 2 byte 増えるだけで済む。
提案が「cable 数だけ先に入れば EspMidi 側は動く」と書いているとおり、両者はコストが
桁違いなので分けた。

### 制約

- 機器の最初の MIDI Streaming インターフェースと、その中の方向ごとに 1 本の bulk endpoint
  だけを追跡する。cable 番号は endpoint ごとに 0 起算なので、同一方向の MS bulk endpoint を
  複数持つ機器は表現できない。送信 API と受信コールバックの対象範囲と同じ制約である。

  インターフェースの選択は以前から `isMidiInterface` の `!device->hasMidiInterface` で
  最初の 1 つに固定されていたが、endpoint 側にはインターフェースの絞り込みが無かった。
  claim のゲートに使われていた `currentClaimResult_` はインターフェース記述子ごとに
  ESP_OK にリセットされ、claim を試みた箇所でしか上書きされないため、claim していない
  2 つ目の MS インターフェースも ESP_OK として通過していた。結果として、MS インターフェースを
  2 つ持つ機器では endpoint 由来のフィールド(`midiOutEndpointAddress`、`midiOutPacketSize`、
  今回追加した cable 数)が後のインターフェースのもので上書きされ、`interfaceNumber` は
  最初のインターフェースのままという食い違いが起きえた。ゲートを
  `currentInterfaceClaimed_` と `currentInterfaceNumber_ == device->midiInterfaceNumber` に
  変更して、追跡対象のインターフェースの endpoint だけを見るようにした。MS インターフェースが
  1 つの機器(通常の機器すべて)では挙動は変わらない。
- cable 数が 0 の場合は、その方向の descriptor が無いか、MS_GENERAL でないか、宣言した
  jack ID を収めるには短いか、cable 番号(4 bit)で指せない本数を宣言している。

