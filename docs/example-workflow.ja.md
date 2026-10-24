# サンプルの進め方

[English](example-workflow.md)

EspUsbHostの例を、配線から結果の確認、改造まで順に試すための案内です。ESP32がUSB機器を制御する側です。キーボード、USBシリアル変換器、USBメモリ等をESP32へ接続します。PCも通常はHostなので、Hostのデータ端子へPCを試験相手として直結する構成ではありません。

## 先に知っておく用語

| 用語 | この案内での意味 |
| --- | --- |
| 列挙（enumeration） | Hostが機器の情報を読み、addressと構成を決める手順 |
| descriptor | 機器が「どんな機能とデータ形式を持つか」を説明する情報 |
| interface | 一つのUSB機器の中の機能単位。複合機器は複数持つことがある |
| endpoint | データの出入口。番号・方向・転送方式を持つ |
| report / packet / frame | 各classやprotocolで決まるデータ単位。同じbyte数とは限らない |
| callback | 事前登録した関数を、libraryがevent等に応じて呼ぶ仕組み |
| buffer / queue | 後で処理するデータを保持する有限の領域・待ち行列 |
| timeout | 相手を待つ時間に上限を設け、応答なしを扱う条件 |

## まず選ぶ例

1. [BringUpCheck](../examples/Info/EspUsbHostBringUpCheck/README.ja.md)でHost開始と相手の列挙を分けて確認します。
2. [DeviceInfo](../examples/Info/EspUsbHostDeviceInfo/README.ja.md)でVID/PID・interface・endpointを読みます。
3. [Keyboard](../examples/HID/EspUsbHostKeyboard/README.ja.md)等の手持ちの機器に合う例を一つ動かします。
4. 未知のHIDは[RawDump](../examples/HID/EspUsbHostHIDRawDump/README.ja.md)と[ReportDescriptor](../examples/Info/EspUsbHostHIDReportDescriptor/README.ja.md)で調べます。
5. 複数機器・hub・音声・独自protocolは、単体の認識と通信を確認してから加えます。

[全サンプル一覧](../examples/README.ja.md)から、各例の必要部品・操作・API・改造時の注意へ進めます。機種固有のhelperがある例は、そのフォルダ全体を使います。

## 導入とビルド

1. [library README](../README.ja.md)の導入手順でArduino-ESP32とライブラリを用意します。対応target・core・確認済み機器はそこで案内される対応資料を参照し、個別例のprofileも確認します。
2. Arduino IDEの「ファイル → スケッチ例 → EspUsbHost」から例を開き、正確なboardを選びます。CLIならリポジトリのルートで次のようにビルドできます。

```sh
arduino-cli compile --profile esp32s3 examples/HID/EspUsbHostKeyboard
```

3. `sketch.yaml`のplatform・libraryはそのprofileのビルド条件です。ローカルlibraryを参照する設定も確認し、外部依存・生成アセットが必要な例は隣のREADMEの準備を行います。ファイル名だけコピーするとhelperが欠ける場合があります。
4. 既知の書き込み経路でuploadし、スケッチの`Serial.begin()`と同じ条件でログを開きます。CLIのビルド成功を、実配線や相互運用の確認とは扱いません。

## コネクタと電源を先に確認する

基板の「USB」という表示だけでは、OTGデータ端子・USB Serial/JTAG・USB-UART bridgeのどれにつながるか分かりません。回路図でcontroller・D−/D+・PHY・VBUS・USB-C CC/role回路を確認します。データ対応ケーブルを使い、端子の信号電圧を合わせます。

Hostは相手機器へ必要なVBUSを供給する側、DeviceはHostへつながる側です。外部電源とUSBの電源出力を不用意に結ばず、boardの給電構成に従います。P4のport・PHY切替は、コネクタの配線や給電回路を自動変更しません。

`Serial`はスケッチの診断経路です。選択board・設定によってUSB経由の場合もUART経由の場合もあるため、どの端子で読めるかを確認します。USB classのCDCやHIDとは別に考えます。Serial/JTAGとOTGの端子を切り替える例では、独立したUART等のログ・復旧経路を先に確保します。

## コードを読む順序

1. 先頭の概要と端子・format・command定数を読みます。値の単位を確認します。
2. global objectの関係を見ます。USB全体のownerと、それに登録する機能・helperを区別します。
3. setup()のcallback登録・媒体やformatの準備・begin()の順を追います。
4. loop()で、受信済みbyte、状態の変化、定期処理、保留している仕事を見ます。
5. 戻り値が成功boolか、実byte数か、状態の問い合わせかを区別します。失敗後に何を保持・破棄するかは例ごとに異なります。

## 状態を取り違えない

| 観察点 | 読み取り方 |
| --- | --- |
| `usb.begin()`成功 | Hostの内部開始。機器が存在・対応するとは限らない |
| 接続通知と機器情報 | 列挙された機器。機能ごとの準備状態は別 |
| classのready/open成功 | そのinterfaceを使う段階。protocol応答の成功とは別 |
| 転送・write成功 | APIが定義する受付・転送の成功。相手アプリの処理結果は応答で確認 |
| 切断 | 古いaddress・interface・sessionの状態を破棄し、再接続で取り直す |

USBのIN/OUTはDeviceから見た方向です。INはDevice→Host、OUTはHost→Deviceです。要求と返信のどちらがどの方向を使うかはprotocolで決まり、送信者の気分で呼び名を入れ替えません。

## callbackとloop()を分ける

このライブラリのUSB callbackは、loop()とは別のUSB処理taskで動きます。登録した関数をloop()から手動で呼んで受信する形ではありません。長いログ・待機・decode・file I/Oをcallbackへ足すと、次のUSB処理を遅らせます。

完了待ちするvendorControlTransfer()・vendorWrite()・MSC・CCIDの操作やend()を、USB clientのcallbackへ追加しないでください。接続通知では仕事を予約し、loop()やアプリtaskから実行します。例が使うcallback対応の設定APIと、完了待ちの転送APIは区別します。

callbackのdata・report・frameは、戻ると再利用される場合があります。後で使う分を自分のbufferへコピーし、所有者と完了点を決めます。非同期・direct転送のbufferは受付後も転送が完了するまで変更しません。コピーする通常writeと、借りるdirect APIの契約を混同しないようにします。

別taskとの共有は`volatile`だけで排他・順序・複数fieldの整合が保証されるわけではありません。FreeRTOS queue等の同期された受け渡しを使い、callbackからは待たずに投入し、満杯時に破棄・計数・再試行のどれをするか決めます。共有領域を守る短いcritical sectionの中で、USB APIやSerialの待機を行わないようにします。

## 有限bufferと失敗の扱い

- readの返却長だけを解釈します。要求長が全部届くことや、一callbackが一命令であることを仮定しません。
- writeが短い・falseの場合は、未送信部分を保持するか、そのデータを捨てたことを数えるかを決めます。例のログ表示だけでは回復処理になりません。
- HIDは押下と解放、MIDIはNote OnとOff、videoはframeの所有期間、audioはsample/frame/byteの違いを確認します。
- storageのblock操作とfilesystemを区別します。ホストとスケッチで同じ媒体を同時に書き換えません。
- callbackで大量にprintして速度を測らないでください。表示の遅さとUSBの遅さを分けます。

## うまくいかないとき

| 症状 | 次の確認 |
| --- | --- |
| ログ自体が読めない | board設定、Serialの経路、端末の通信条件、port切替の影響 |
| begin()が失敗 | 直後のエラー名、controller設定、他stackとの競合、必要な資源 |
| 開始は成功するが列挙されない | データ端子、ケーブル、VBUS・役割、選択controllerとPHY |
| 列挙するがclassが使えない | interface・endpoint・report descriptor、相手の対応format |
| 最初だけ動く・負荷で欠ける | 実受信長、返却byte数、queueの満杯、callback時間、buffer寿命 |
| 再接続後に動かない | 古いaddress・session・DTR・保持キー・保留転送の状態 |

相手なし、接続、通常操作、切断、再接続の順で一つずつ試し、各段階のログを控えます。device-specificな設定変更・印刷・Flash更新等は、どの実動作が起きるかを個別READMEで確認してから行います。

詳しい原理は[USB開発ガイド](usb-host-guide.ja.md)、callbackや転送の実装契約は[上級ガイド](usb-host-advanced.ja.md)、公開APIは[library README](../README.ja.md)を参照してください。版ごとの対応や実測値は、それぞれの専用資料へ集約します。
