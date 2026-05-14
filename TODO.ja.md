# exampleフォルダの整理

EspUsbHostCustomHIDを削除してHIDRawDumpに統合

# キーボード

LEDまわりをライブラリ側で管理する
対応言語の追加

extern const uint8_t KeyboardLayout_de_DE[];
extern const uint8_t KeyboardLayout_en_US[];
extern const uint8_t KeyboardLayout_es_ES[];
extern const uint8_t KeyboardLayout_fr_CH[];
extern const uint8_t KeyboardLayout_fr_FR[];
extern const uint8_t KeyboardLayout_it_IT[];
extern const uint8_t KeyboardLayout_pt_PT[];
extern const uint8_t KeyboardLayout_sv_SE[];
extern const uint8_t KeyboardLayout_da_DK[];
extern const uint8_t KeyboardLayout_hu_HU[];
extern const uint8_t KeyboardLayout_pt_BR[];

Arduino側のデバイスは上記
JAがないのでPRする必要あり

# サンプル充実
① Mouse — 累積座標 (accumulated position)
現状: dx / dy のデルタ値を垂れ流すだけ
追加: posX / posY を保持して絶対座標として表示、クリック検出（ボタンの立ち上がりエッジ）
実装コスト: ほぼゼロ。使い道が一番広い

② MIDI — ノート名変換
現状: ノート番号・チャンネル・ベロシティを数字で表示
追加: ノート番号 60 → C4、69 → A4 のような名前変換。シリアル入力で1ノート鳴らすのも現状できているので、C4 vel=127 のような入力形式にするとデモとして使いやすい
実装コスト: 変換テーブル or 演算12行程度

③ Gamepad — デッドゾーン + 正規化
現状: 生の軸値をそのまま表示
追加: 閾値未満を0扱いするデッドゾーン、-1.0〜1.0 の正規化値を並列表示
実装コスト: 小さい。実機アプリでは必須なのでサンプルとして価値がある
