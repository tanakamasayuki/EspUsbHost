# EspUsbHostでのUSBディスプレイ

> English: [usb-display.md](usb-display.md)

USBディスプレイにはデバイスクラスがありません。個体ごとにトランスポートもプロトコルも異なるため、ライブラリ本体にはディスプレイ固有の処理を一切入れていません。それぞれを汎用トランスポートAPIの上に載せたexampleとして提供し、このページがその一覧です。

exampleは**機能別ではなくトランスポート別**に置いています。DL-1xxアダプタはvendor classのbulkデバイスなので `examples/Vendor/` に、スマートスクリーンはCDCシリアルなので `examples/Serial/` に置きます。こうすると同じライブラリAPIを使う他のexampleの隣に並びます。機能でまとめる役割はこのページが担います。

## example一覧

| example | ハードウェア | トランスポート | 解像度 | 使用するライブラリAPI |
|---|---|---|---|---|
| [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) | DisplayLink DL-1xxチップ搭載USBグラフィックスアダプタ（`17e9:*`） | vendor class、bulk OUT | 最大1920x1080 | `vendorOpen()`、`vendorControl*()`、`vendorWriteQueue*()` |
| [`EspUsbHostDisplayTuring`](../examples/Serial/EspUsbHostDisplayTuring/) | 3.5インチUSBスマートスクリーン — Turing Smart Screen revision A と、`USB35INCHIPSV2` を名乗る互換機（`1a86:5722`） | CDC ACMシリアル | 320x480 | `setSerialBaudRate()`、`serialWriteQueue*()` |
| [`EspUsbHostDisplayAx206`](../examples/Vendor/EspUsbHostDisplayAx206/) | AX206のUSBフォトフレーム（`1908:0102`） | bulk上のBulk-Only Transport、class `0xdc` | 480x320 | 番号指定＋オンデマンド読みの `vendorOpen()`、`vendorReadSync()`、`vendorWriteQueue*()` |

3つともディスプレイを `lgfx::Panel_Device` の派生クラスとして公開し、[LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas) と組み合わせて使う想定です。LGFXVirtualCanvasは画面を1つの小さなスプライトで水平バンドに分けて描くため、ホスト側にフルサイズのフレームバッファが不要になり、前の2つでは差分転送で変化のないバンドを省略できます。

## どれを選ぶか

**Full HD、またはピクセル数の多いものを扱うなら DL-1xx。** 3つの中で唯一、転送が律速にならないデバイスです。一般的なUI内容ならRLEで5〜20倍に圧縮されるので、バスの使用率は数%にとどまり、コストは描画コールバック側にあります。この解像度に届くのは他にありません。その代わり3つの中で最も複雑です（モード設定、タイミングレジスタ、EDID）。

**それ以外は3.5インチスマートスクリーン（Turingおよび互換機）。** 通常はこれを推奨します。3つの中で最も安価で流通量も多く、2Dの矩形で部分更新を受け付けるため差分転送が効きます。つまり変化したバンドしかコストになりません。CDCシリアルなので扱いも一番簡単です。

**AX206フォトフレームは、それが手元にある場合に限り。** 動作はしますが、受け付けるのは全画面blitだけです。どれだけ変化が小さくても毎フレーム307,200バイトで、full-speedホストでは0.53秒かかります。描画をどう工夫しても動かない2 fps固定で、差分転送も一切使えません。ゆっくり変わるステータス表示なら十分ですが、動きのあるものには向きません。

## 共通点

3つとも自前のフレームバッファを持ち、USB転送が完全に止まっても表示を保持します。そのためどのpanel実装もフレームバッファを持たず、描画操作をそのままワイヤ上のコマンドに変換します。3つとも読み戻し非対応なので、`readRect()`、ARGB合成、`copyRect()` はいずれでも使えません。

3つとも同期writeではなく非同期の書き込みキュー経由で送出します。理由も同じで、ディスプレイの書き手はfull-speedのendpointを追い越すため、事前確保された有限のプールがないと押し戻しが効きません。キューはどれも同じ形をしています。`…WriteQueueBegin()`、ゼロコピー経路の `…WriteAcquire()` / `…WriteSubmit()`、`…WriteFlush()`、そして `EspUsbHostWriteQueueStats` のスナップショットです。

## 相違点

|  | DL-1xx | 3.5インチスマートスクリーン | AX206 |
|---|---|---|---|
| アドレッシング | デバイスフレームバッファへの線形バイトアドレス | 2Dの矩形（両端を含む） | 全画面のみ |
| 部分更新 | 可 | 可 | 不可 |
| 圧縮 | RLE。一般的なUI内容で5〜20倍 | なし。つねに1ピクセル2バイト | なし。つねに1ピクセル2バイト |
| ピクセル順 | ビッグエンディアンRGB565（`rgb565_2Byte`） | リトルエンディアンRGB565（`rgb565_nonswapped`） | ビッグエンディアンRGB565（`rgb565_2Byte`） |
| 回転 | 非対応 | パネル側が実施（`setOrientation()`） | 非対応 |
| モード設定 | タイミングレジスタ、EDID読み出し可 | 固定パネル、向きのみ | 固定パネル |
| ステータスフェーズ | なし | なし | 全コマンドの末尾で読み戻す |
| 差分転送 | 有効 | 有効 | 使用不可 |
| 律速要因 | バンドごとの描画コールバック | USB転送 | USB転送 |

実務上重要なのは最後の行で、チューニングの指針が逆になります。DL-1xxアダプタではバスは数%しか使われず描画がコストなので、バンドは少なく大きくするのが有利です。スマートスクリーンでは1ピクセルがワイヤ上で2バイトかかり、パネルが0.155 MB/sの固定レートでペースを決めるため、効くのは送るピクセルを減らすことだけです。矩形への分割の仕方はコストを変えません。AX206ではそれすら効きません。送るピクセル数を選ぶ余地がないからです。各exampleのREADMEに実測のスイープがあります。

スマートスクリーンにはDL-1xxにはないフレーミング上の要求が2つあり、どちらも資料には記載がありません。前の矩形のピクセルが届ききる前に届いたコマンドは捨てられること、そしてコマンドは単独のUSB転送を占有しなければならないことです。どちらもエラーにも転送欠落にもならず黙って壊れるため、exampleのREADMEで説明し、manualテストで両方を検出できるようにしています。

## ディスプレイを追加するには

ライブラリ側は意図的に汎用です。vendor classのbulkデバイスなら `vendorWriteQueue*()` が、CDCシリアルなら `serialWriteQueue*()` がすでにあります。新しいexampleはトランスポートに対応するディレクトリに置き、プロトコル層はArduino / LovyanGFX / USBに依存しない形にしてhostのg++で単体テストできるようにしてください（`tests/unit/dl1xx`、`tests/unit/turing`、`tests/unit/ax206` を参照）。そのうえで上の表に行を追加してください。

プロトコルのサンプルではなく実用的なディスプレイスタックが必要な場合（対応アダプタの多さ、高いフレームレート）は、[Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) を使ってください。

## 設計資料

- [`usb-display-spec.ja.md`](usb-display-spec.ja.md) — DL-1xxの設計文書・開発記録。参照元とライセンス、実測したUSB構成、コントロール要求、`0xAF` bulkコマンド、ビデオレジスタとLFSR16エンコード、RLE形式、表示保持、Full HD実現性の数値根拠に加え、テスト計画・フェーズ分割・未解決事項を含みます。
- [`usb-display-spec.md`](usb-display-spec.md) — 上記のうちプロトコル調査部分の英語版です。
