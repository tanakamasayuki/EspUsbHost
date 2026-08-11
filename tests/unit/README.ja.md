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

- `turing`: `examples/Serial/EspUsbHostDisplayTuring` の3.5インチUSB
  スマートスクリーンのプロトコル層(`TuringProtocol.hpp`)を検証する。10bit
  座標4つを含む6バイトコマンドパケット(独立実装のデコーダとのround tripを、
  1フィールドずつと、パネルの座標空間全域の網羅で実施)、両端を含む
  DISPLAY_BITMAPの矩形、パネルサイズとパッキング上限の両方に対する範囲ガード、
  ビッグエンディアンのサイズを持つ11バイトの向き設定パケット、ワイヤ上で逆順に
  なる輝度レベル、LovyanGFXのrgb565_nonswapped出力をバイトスワップなしでUSBへ
  流せる根拠であるRGB565リトルエンディアンのピクセルバイト列を対象とする。

- `ax206`: `examples/Vendor/EspUsbHostDisplayAx206` のAX206 USBディスプレイの
  プロトコル層(`Ax206Protocol.hpp`)を検証する。MIT参照実装からバイト単位で
  再現した2つの16バイトvendorコマンドブロック(他はすべてここから導出される)、
  両端を含むblit矩形とそのデータ長、範囲ガード、リトルエンディアンのtagと
  転送長・方向フラグ・LUN・コマンド長を持つ31バイトのCommand Block Wrapper、
  オフセットではなくシグネチャで探すCommand Status Wrapper(tag不一致・切り詰め・
  先頭のゴミバイトを含む)、LovyanGFXのrgb565_2Byte出力をバイトスワップなしで
  USBへ流せる根拠であるRGB565ビッグエンディアンのピクセルバイト列を対象とする。

- `usbtmc`: `examples/Vendor/EspUsbHostUsbtmcScpi` のUSBTMCメッセージ層
  (`UsbtmcProtocol.hpp`)を検証する。全メッセージが従う4バイト境界、0にならず
  直前と重複もしないbTag列、リトルエンディアンのTransferSizeとEOM/TermChar属性を
  含むDEV_DEP_MSG_OUT・REQUEST_DEV_DEP_MSG_INヘッダのバイト単位一致、フィールド
  レイアウトから独立に組んだヘッダに対する応答解析と、信用せず拒否すべき同期ずれ
  全パターン(MsgID不一致・bTagInverse不正・bTag 0・読み取り長を超えるTransferSize)、
  GET_CAPABILITIESのビットフィールド、および実機PMX18-5Aが返すバイト列を
  USB488フィールドのオフセット回帰(bcdUSB488が12-13を占めるため14/15にある)として
  対象とする。

- `ccid_atr`: `src/EspUsbHostCcidAtr.h` のCCID ATRパーサを検証する。実機のSony
  RC-S300 + ISO 14443 Aカードで取得したATR(標準・レベル・カード名・宣言された
  プロトコルへの分解)、PC/SC PIX.SSのISO 14443 A/B・ISO 15693・ISO 7816-10メモリ
  カード・FeliCa・低周波非接触への対応付け、表にない標準コードをISO 7816カードと
  誤って報告せず生値のまま返すこと、接触カード自身のATR(interface byteを辿って
  historical bytesを見つける、T=1検出、TDが無い場合のT=0既定)、および不正入力の
  拒否(null、T0欠落、不正なTS、historical bytesの不足、TD1欠落、RID不一致、
  historical bytesより長いTLV)を対象とする。

- `felica_idm`: `examples/Ccid/EspUsbHostCcidFelicaIdm` のFeliCa IDm例が持つ2つの
  protocol層(`FelicaProtocol.hpp`・`Rcs300Protocol.hpp`)を検証する。交通系System
  Code 0x0003とワイルドカード0xffffのFeliCa Pollingフレーム(自身を含む長さバイトを
  含む)、リクエストデータ有無それぞれのPolling応答と応答元System Code、および不正
  入力の拒否(応答コード不一致、宣言長が短すぎる・長すぎる・バッファを超える)、
  `tests/probe/rcs300_felica` が実機に送ったものと同一のRC-S300 transparent session
  疑似APDU全て(manage session 4種、FeliCaへのswitch protocol、Pollingを載せた
  transparent exchange)、実機が実際に返した応答オブジェクト(受理、8F 01 08を返す
  switch protocol、フィールドが空のexchange、拒否の6301 / 6401 / 6700 / 6A81)、
  成功時のexchangeを応答バイトからIDmまで復号する経路を対象とする。

- `mouse_layout`: `src/EspUsbHostHidLayout.h` のマウスreport descriptorパーサと
  レポートデコーダを検証する。boot mouseのdescriptorから、従来の固定boot解釈と
  完全に同じレイアウトが得られること、issue #39で報告されたLogitech G502 HEROの
  レイアウト(16ボタン・16bit X/Y・wheel・AC Panの8バイト)と、それが引き起こして
  いた2つの不具合(縦移動だけのレポートが無変化に見えること、X移動がwheelに出る
  こと)、report ID(キーボードとマウスの複合descriptor、レポート本体基準のビット
  オフセット、別IDのレポートを拒否すること)、Generic DesktopのX / Yを持つjoystick
  collectionをマウスと誤認しないこと、バイト境界をまたぐ12bit軸の符号拡張と
  Push / Pop、および不正入力の拒否(null・空・切り詰めたdescriptor、Yの無い
  collection、無効なlayoutでのデコード)を対象とする。

- `audio_uac`: `src/EspUsbHost.h` のUSB Audio descriptor / controlデコーダを検証する。
  両class revisionのFeature Unit `bmaControls` レイアウト(UAC1は`bControlSize`の
  stride、UAC2は4バイト固定。UAC2のdescriptorをUAC1として読んだ場合に拒否されること
  も含む)、control maskの解釈(UAC1は1 control 1ビット、UAC2は2ビットで有無・読取
  専用・設定可)、explicit feedback endpointを識別するisochronousのusage type
  (implicit feedback dataと混同しないこと)、feedback payloadのデコード(3バイトは
  10.14、4バイトは16.16。full speedは1フレームあたり、high speedはmicroframe
  あたり。小数値と44.1 kHz、短いpayloadとnull)とレート追従を守る±12.5%の窓、
  およびUAC2の`RANGE`応答
  (`wNumSubRanges`、離散レート、resolutionで刻む連続subrange、切り詰められた
  payload、重複およびゼロのレート、呼び出し側の容量上限、signed 1/256 dBのvolume
  range)を対象とする。あわせて`audioInputStart()` / `audioOutputStart()`が委譲する
  `espUsbHostSelectAudioStreamForFormat()`も検証する(完全一致、`0`=指定なしの
  wildcard、幅やレートが異なるalt間のランク付け、指定レートがスコア優先より優先される
  こと、連続range、`startable == false`のフォーマット専用altが除外されること)。

## 仕組み

`dl1xx`・`ax206`・`felica_idm`・`usbtmc`のヘッダと`src/EspUsbHostCcidAtr.h`・
`src/EspUsbHostHidLayout.h`はArduino/USB非依存の純粋なバイト処理なので、
`test_dl1xx.py`・`test_ax206.py`・`test_felica_idm.py`・`test_usbtmc.py`・
`test_ccid_atr.py`・`test_mouse_layout.py`はそのままg++でコンパイルでき、抽出は
不要である。

`src/EspUsbHost.h`はArduinoとESP USB hostスタックをincludeするため、`audio_uac`は
必要なaudioの定数・struct・`inline`デコーダを`output/espusbhost_audio_real.h`へ
抽出する(keymapテストと同じ方式)。

`src/EspUsbHostHid.cpp`は`Arduino.h`とESP USB hostスタックをincludeするため、host上で
そのままコンパイルできない。ロジックを複製せずに実コードを検証するため、
`test_keymap.py`が実ソースから`EspUsbHostKeyboardLayout` enum・`keymap/*.h`のinclude
一覧・`MOD_*`定数・純粋な変換関数(keypad・`espUsbHostKeycodeToUnicode`・`espUsbHostKeycodeToAscii`ラッパー)を抽出し、`output/espusbhost_keymap_real.h`に
結合して`keymap_test.cpp`と一緒にビルドする。よってアサーションはproductionのtableと
ロジックそのものを検証する。`stub/`はTinyUSBの`<class/hid/hid.h>`のhost用スタブ
(en_USフォールバックtable。本テストでは検証対象外)を提供する。
