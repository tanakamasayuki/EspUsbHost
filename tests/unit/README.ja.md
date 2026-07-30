# Unit Tests

[English](README.md)

host上で実行する純粋C++/データ変換のテストです。実機とシリアルポートは不要で、g++でビルドして実行します。

```sh
uv run --env-file .env pytest unit/
```

## テスト

- `keymap`: `src/EspUsbHostHid.cpp`のHID usage→8-bit文字変換を、各layoutの一次ソース
  (Windows layoutデータ)由来の期待値で検証する。ja_jp・nl_NL(Windows KBDNE)・
  pt_BR(ABNT2)のtableを対象とし、dead key(→0)、ISO-8859-1の非ASCII値、ABNT2の
  International1 `/ ?` とnumpad comma、`keycode >= 0x80`の範囲ガード
  (`0x80..0x8f`に到達できるのはja_jp/pt_BRのみ)を固定する。

- `dl1xx`: `examples/Vendor/EspUsbHostDisplayDl1xx` のDL-1xxプロトコル層
  (`Dl1xxProtocol.hpp`、`Dl1xxModes.hpp`)を検証する。タイミングレジスタ値を
  エンコードする16bit LFSR(参照値と、tap setを固定する最大長性質)、ピクセル
  クロックのlow byte firstという例外を含むレジスタ書き込みのバイト順、RLE
  ピクセルエンコーダ(仕様書に載っている単色256pxの10バイト形、最悪ケース519
  バイト、独立実装のデコーダとのround trip、バッファ上限とcanaryによる範囲外
  書き込み検出)、Full HDのモード設定レジスタ列を対象とする。

## 仕組み

`src/EspUsbHostHid.cpp`は`Arduino.h`とESP USB hostスタックをincludeするため、host上で
そのままコンパイルできない。ロジックを複製せずに実コードを検証するため、
`test_keymap.py`が実ソースから`EspUsbHostKeyboardLayout` enum・`keymap/*.h`のinclude
一覧・`MOD_*`定数・純粋な変換関数(keypad・`espUsbHostKeycodeToUnicode`・`espUsbHostKeycodeToAscii`ラッパー)を抽出し、`output/espusbhost_keymap_real.h`に
結合して`keymap_test.cpp`と一緒にビルドする。よってアサーションはproductionのtableと
ロジックそのものを検証する。`stub/`はTinyUSBの`<class/hid/hid.h>`のhost用スタブ
(en_USフォールバックtable。本テストでは検証対象外)を提供する。
