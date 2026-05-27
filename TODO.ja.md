# チャンネル数チェック

(1)現状把握APIを追加する
claim済みinterfaceの bNumEndpoints 合計を返すAPI
ライブラリが実際にtransferを張っているendpoint数を返すAPI
claim成功/失敗時のログに推定カウントを出す
ここでは制御ロジックを入れない

(2)実験してカウントの正確性を確認する
Hubのみ、HID、CDC、MSC、MIDI、Audioなどを順に追加
推定カウントとESP-IDFの失敗ログを比較
8 に近いところで失敗するのか、もっと早いのかを見る

(3)どのタイミングで制御すべきか検討する
interface_claim 前で止めるべきか
device単位で除外すべきか
endpoint単位の抑制に意味があるか
Audioのように遅延claimした方がよいものがあるか確認する

(4)制御用コールバックを追加する
設定フラグは増やさない
関連コールバック未登録なら自動で開かない方向にする
必要なら onBeforeDeviceUse() / onBeforeInterfaceClaim() を追加する
endpoint単位 callback は、実験結果を見て必要なら追加

# USB Mass Storage / FAT

方針:
FAT自体は自前実装しない。
ESP-IDFに入っているFatFs/VFSを流用し、EspUsbHostのMSC block I/OをFatFsのdisk I/O層へ接続する。
Espressifの `usb_host_msc` コンポーネントは、BOT/SCSI復旧処理とVFS連携の設計を参考にする。

(1) block I/Oの信頼性を先に固める
CSW failed / phase error の扱いを整理する
Bulk-Only Mass Storage Reset と bulk endpoint clear halt の復旧経路を追加する
`REQUEST SENSE` の結果をユーザーが参照できるAPIを追加済み。エラー復旧時の使い方を詰める
timeout、短い転送、CSW tag mismatch、disconnect中のread/writeをテストする
USBMSC peerで再現できる範囲として、write失敗を成功扱いしない自動テストを追加済み。write失敗後の完全復旧、invalid CSW/tag mismatch/phase error/stallは低レベルpeerが必要
write系はpeerテストと専用メディアのmanualテストで確認済み。破壊的なテストは引き続き専用メディアに限定する

(2) LUNと容量周りを拡張する
`GET_MAX_LUN` control requestを追加済み
LUN指定つきのcapacity/read/write APIを追加済み。複数LUN実デバイスでのmanual確認は未実施
64bit LBA向けに `READ(16)` / `WRITE(16)` を追加済み
512 bytes以外のblock sizeでread/writeできるか確認する

(3) FatFs disk I/O adapterを作る
FatFsのdrive番号とMSCデバイス/address/LUNを対応付ける管理テーブルを追加済み
disk read/writeを `mscReadBlocks64()` / `mscWriteBlocks64()` へ接続済み
disk ioctlでsector count、sector size、syncを返す実装を追加済み
MBR/FATパーティションはFatFs側に任せる方針で、実USBメモリmanualテスト済み
複数MSCデバイス同時接続時のdrive割り当てを実機確認する。ESP32-S3はHCDチャネル数で厳しい可能性があるため優先度を下げ、ESP32-P4での確認を優先する

(4) mount/unmount APIを追加する
`mscMount("/usb")`, `mscUnmount("/usb")` の最小APIを追加済み
内部では `esp_vfs_fat_register_cfg()` と `f_mount()` を使う
disconnect時にFatFs/VFS登録を自動解除する処理と、mount中に抜いて再接続後に再mountするmanualテストを追加済み
open中ファイルやwrite cacheがある状態で抜かれた場合の挙動を明記する
format/mkfsは最初は入れず、必要になったら別APIとして検討する

(5) Arduino向けAPIを検討する
まずはVFS経由の `fopen()` / POSIX API が使える状態を優先する
その後、必要なら `fs::FS` / `File` 互換のwrapperを追加する
既存のSDライブラリをそのまま流用するのではなく、内部をFatFs/VFSに接続する薄いwrapperにする

(6) サンプルとテスト
ファイル一覧と一時ファイルwrite/read/deleteのサンプルを追加済み
専用USBメモリ向けのファイルwrite/read/delete manualテストを追加済み
peerテストではFATイメージを持つMSCデバイスを用意できるか検討する
TEST_PLANにFS統合後のカバレッジを反映済み
READMEに「block I/O」と「FAT/VFS対応」の境界を反映済み

# USB Audio


主な残作業はこのあたりです。

USB Audio IN の peer 実証
→Arduinoのデバイス側マイクがおかしいかも。それの影響でPeerテストができていない可能性がある。

onAudioData() の受信経路はありますが、Arduino 側 USBAudioCard.write() で peer から実データを流す確認がまだ安定していません。
今 peer で確実に通っているのは Host → Device の Audio OUT です。
Mixer / Selector / Processing Unit 制御

UAC1 Feature Unit の Mute / Volume GET/SET は追加済みです。
Mixer / Selector / Processing Unit や、Feature UnitのBass/Treble/AGCなどは未対応です。
Clock Source / UAC2 対応

今の実装は主に UAC1 Type I 前提です。
UAC2 デバイスでは Clock Source / Clock Selector 周りが必要になる場合があります。
フォーマット選択の強化

実機互換性テスト

USBスピーカー、USBマイク、USBオーディオIFでの手動確認。
特に alt setting、サンプルレート、最大パケットサイズ、同期方式で差が出やすいです。
Audio Input サンプルの実受信確認

Input サンプルはビルドと情報表示までは整えました。
実USBマイクで audio: ... bytes_per_sec=... が継続して出るか確認したいです。
ドキュメントの対応範囲明記

OUT は peer で送信確認済み。
IN は API あり、peer 実データは未確定。
UAC1中心、UAC2やFeature Unit以外のAudio Controlは限定的、という注意書きをもう少し明確にしてよさそうです。

# P4対応

USB HSだとUSB HUBが実質使えない
FS側だと使える
ループバックテストでデバイスとHOST両方でどこまで動いているか個別確認をする
