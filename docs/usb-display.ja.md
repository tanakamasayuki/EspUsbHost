# EspUsbHostでのUSBディスプレイ

English: [usb-display.md](usb-display.md)

USBディスプレイにはデバイスクラスがありません。個体ごとにトランスポートもプロトコルも異なるため、ライブラリ本体にはディスプレイ固有の処理を一切入れていません。それぞれを汎用トランスポートAPIの上に載せたexampleとして提供し、このページがその一覧です。

exampleは**機能別ではなくトランスポート別**に置いています。DL-1xxアダプタはvendor classのbulkデバイスなので `examples/Vendor/` に、スマートスクリーンはCDCシリアルなので `examples/Serial/` に置きます。こうすると同じライブラリAPIを使う他のexampleの隣に並びます。機能でまとめる役割はこのページが担います。

## example一覧

| example | ハードウェア | トランスポート | 解像度 | 使用するライブラリAPI |
|---|---|---|---|---|
| [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) | DisplayLink DL-1xxチップ搭載USBグラフィックスアダプタ（`17e9:*`） | vendor class、bulk OUT | 最大1920x1080 | `vendorOpen()`、`vendorControl*()`、`vendorWriteQueue*()` |
| [`EspUsbHostDisplayUsb35Inch`](../examples/Serial/EspUsbHostDisplayUsb35Inch/) | 3.5インチUSBスマートスクリーン `USB35INCHIPSV2`（`1a86:5722`） | CDC ACMシリアル | 320x480 | `setSerialBaudRate()`、`serialWriteQueue*()` |

どちらもディスプレイを `lgfx::Panel_Device` の派生クラスとして公開し、[LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas) と組み合わせて使う想定です。LGFXVirtualCanvasは画面を1つの小さなスプライトで水平バンドに分けて描くため、ホスト側にフルサイズのフレームバッファが不要になり、さらに重要な点として差分転送で変化のないバンドを省略できます。

## 共通点

どちらのデバイスも自前のフレームバッファを持ち、USB転送が完全に止まっても表示を保持します。そのためどちらのpanel実装もフレームバッファを持たず、描画操作をそのままワイヤ上のコマンドに変換します。どちらも読み戻し非対応なので、`readRect()`、ARGB合成、`copyRect()` は両方で使えません。

どちらも同期writeではなく非同期の書き込みキュー経由で送出します。理由も同じで、ディスプレイの書き手はfull-speedのendpointを追い越すため、事前確保された有限のプールがないと押し戻しが効きません。2つのキューは同じ形をしています。`…WriteQueueBegin()`、ゼロコピー経路の `…WriteAcquire()` / `…WriteSubmit()`、`…WriteFlush()`、そして `EspUsbHostWriteQueueStats` のスナップショットです。

## 相違点

|  | DL-1xx | 3.5インチスマートスクリーン |
|---|---|---|
| アドレッシング | デバイスフレームバッファへの線形バイトアドレス | 2Dの矩形（両端を含む） |
| 圧縮 | RLE。一般的なUI内容で5〜20倍 | なし。つねに1ピクセル2バイト |
| ピクセル順 | ビッグエンディアンRGB565（`rgb565_2Byte`） | リトルエンディアンRGB565（`rgb565_nonswapped`） |
| 回転 | 非対応 | パネル側が実施（`setOrientation()`） |
| モード設定 | タイミングレジスタ、EDID読み出し可 | 固定パネル、向きのみ |
| 律速要因 | バンドごとの描画コールバック | USB転送 |

実務上重要なのは最後の行で、チューニングの指針が逆になります。DL-1xxアダプタではバスは数%しか使われず描画がコストなので、バンドは少なく大きくするのが有利です。スマートスクリーンでは1ピクセルがワイヤ上で2バイトかかり、パネルが0.155 MB/sの固定レートでペースを決めるため、効くのは送るピクセルを減らすことだけです。矩形への分割の仕方はコストを変えません。各exampleのREADMEに実測のスイープがあります。

スマートスクリーンにはDL-1xxにはないフレーミング上の要求が2つあり、どちらも資料には記載がありません。前の矩形のピクセルが届ききる前に届いたコマンドは捨てられること、そしてコマンドは単独のUSB転送を占有しなければならないことです。どちらもエラーにも転送欠落にもならず黙って壊れるため、exampleのREADMEで説明し、manualテストで両方を検出できるようにしています。

## ディスプレイを追加するには

ライブラリ側は意図的に汎用です。vendor classのbulkデバイスなら `vendorWriteQueue*()` が、CDCシリアルなら `serialWriteQueue*()` がすでにあります。新しいexampleはトランスポートに対応するディレクトリに置き、プロトコル層はArduino / LovyanGFX / USBに依存しない形にしてhostのg++で単体テストできるようにしてください（`tests/unit/dl1xx` と `tests/unit/usb35inch` を参照）。そのうえで上の表に行を追加してください。

プロトコルのサンプルではなく実用的なディスプレイスタックが必要な場合（対応アダプタの多さ、高いフレームレート）は、[Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) を使ってください。

## 設計資料

- [`usb-display-spec.ja.md`](usb-display-spec.ja.md) — DL-1xxの設計文書。プロトコル調査、ライセンス、Full HD実現性の数値根拠、フェーズ分割。
