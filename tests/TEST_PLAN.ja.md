# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

## テスト方針

テストはソフトウェアで環境を完全に制御できるかどうかに基づいて2種類に分けます。

**自動テスト**はCIまたはローカルで人の操作なしに実行します。
入力はすべてプログラムで生成し、期待される出力はすべてアサーションで検証します。

**手動テスト**は環境をソフトウェアで完全に制御できない場合に使います。
「自動化が面倒だから」ではなく、エミュレートできない物理ハードウェアが必須である、または人の判断（目視確認・触覚フィードバックなど）による検証が本質的に必要である場合に限ります。

### EspUsbDevice とのテスト分担

`EspUsbHost` 側の `peer/` テストは、原則として Arduino-ESP32 標準 USB Device 実装を使った
現在の構成を維持します。Host ライブラリが兄弟ライブラリ `EspUsbDevice` だけに過適応することを避け、
Arduino Core 標準 Device 実装との組み合わせで基本的な相互運用性を確認するためです。

`EspUsbDevice` 側では、このリポジトリの released `EspUsbHost` と組み合わせて、descriptor、
report ID、output / feature report、複合 HID、CDC、MIDI、MSC など、Arduino Core 標準 Device
実装では制御しづらい項目を詳しく検証します。ESP32-P4 1台構成の loopback テストも、現在は
`EspUsbDevice` 側を主な整備場所にします。

ESP32-P4 loopback をこのリポジトリ側で維持しない理由は、Arduino-ESP32 標準 USB Device 実装が
P4 では HS 側でのみ動作するためです。1台構成で直結すると Device が HS 側を使い、Host は FS 側に
固定されますが、FS host では HS device を処理できず endpoint claim / allocation が失敗します。
これは loopback 直結時だけの制約であり、USB Host または標準 USB Device 機能を個別に使う場合の
一般的な問題ではありません。

Host 側の未リリース修正を `EspUsbDevice` で先行確認する場合は、切り分け目的でローカル checkout
を使ってよいですが、通常の合格条件は released `EspUsbHost` と Arduino Core 標準 Device 実装の
組み合わせを基準にします。

```
tests/
  peer/       自動 — ESP32-S3 2台構成（1台ホスト + 1台デバイス）
  loopback/   予約 — ESP32-P4 1台構成用。現在このリポジトリには実行可能テストなし
  manual/     手動 — 特殊ハードウェアまたは人による操作が必要
```

ハードウェアの接続方法や個別テストの詳細は、各サブディレクトリの README を参照してください。

---

## テストカバレッジ一覧

| 機能 | 自動 | 手動 | 未カバー |
|------|------|------|---------|
| HIDキーボード入力 | ✅ peer | | |
| HIDキーボードレイアウト（JP） | | | ⬜ |
| HIDマウス入力 | ✅ peer | | |
| HIDコンシューマーコントロール | ✅ peer | | |
| HIDシステムコントロール | ✅ peer | | |
| HIDゲームパッド | ✅ peer | | |
| HIDベンダー入出力 | ✅ peer | | |
| HID生データダンプ | ✅ peer (custom_hid) | | |
| キーボードLED出力 | ✅ peer (hid_logic) | ✅ manual（目視） | |
| USBシリアル — CDC ACM | ✅ peer、line coding設定 | | |
| USBシリアル — VCP（FTDI・CP210x・CH34x） | | ✅ manual、シリアル形式設定 | |
| USB MIDI | ✅ peer | | |
| USBオーディオ入出力 | ✅ peer（出力）、入力は一部 | | |
| USB Mass Storage — ブロックI/O / FatFsマウント | ✅ peer（容量、Inquiry/Sense、read/write、範囲外拒否、write失敗検出） | ✅ manual（実USBメモリの容量取得、LBA0 read、FatFs/VFS mount、`fs::FS` wrapper、ファイルwrite/read/delete、mount中disconnect/remount） | ⬜ data phase失敗後の完全なBOT復旧、複数LUN、32-bit sector超のFatFs mount |
| 複数デバイス同時接続 | | ✅ manual | |
| デバイス活線挿抜 | | ✅ manual | |
| HUB検出・トポロジー・ポート電源制御 | | ✅ manual（`hub_info`、`hub_power`） | ⬜ change bit clear、複数段Hub、USB 3.x Hub互換性 |
