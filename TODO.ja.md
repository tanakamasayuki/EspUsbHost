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

# USB Hub

状態:
基本実装済み。Hub検出、`device.isHub`、`parentAddress`/`portId`による簡易トポロジー、Hub descriptor取得、port status取得、PPPS対応Hubのポート電源ON/OFF、`hub_info`/`hub_power` manualテストまで完了。

残作業:
port change bitをclearするAPIまたは内部処理が必要か確認する
ganged power Hubでの実機挙動を複数機種で確認する
複数段HubとUSB 3.x Hubの互換性を確認する
ESP32-P4のFS/HS OTGでHub可否を確認する
チャンネル数・endpoint使用量の可視化と合わせて、Hub配下でどの構成まで動くか整理する

# USB Mass Storage / FAT

状態:
基本実装済み。単一MSCデバイスのブロックI/O、FatFs/VFSマウント、Arduino `fs::FS` / `File`互換ラッパー、サンプル、peer/manualテスト、README反映まで完了。
非準拠MSC向けに、FatFs同期時のSCSI `SYNCHRONIZE CACHE(10)`をmount単位でスキップする互換モードと、失敗時の自動フォールバックを追加済み。

方針:
FAT自体は自前実装しない。
ESP-IDFに入っているFatFs/VFSを流用し、EspUsbHostのMSC block I/OをFatFsのdisk I/O層へ接続する。
Espressifの `usb_host_msc` コンポーネントは、BOT/SCSI復旧処理とVFS連携の設計を参考にする。

完了済み:
`GET_MAX_LUN`、LUN指定つきcapacity/read/write、64bit LBA向け `READ(16)` / `WRITE(16)`、`REQUEST SENSE` 参照API、BOT reset recovery、FatFs disk I/O adapter、`mscMount()` / `mscUnmount()`、`EspUsbHostMscFS`、disconnect/remount manualテスト、write/read/deleteサンプル、README反映

残作業:
複数LUN実デバイスでmanual確認する
512 bytes以外のblock sizeでread/writeできるか確認する
複数MSCデバイス同時接続時のdrive割り当てを実機確認する。ESP32-S3はHCDチャネル数で厳しい可能性があるため、ESP32-P4での確認を優先する
peerテストでFATイメージを持つMSCデバイスを用意できるか検討する
format/mkfsは必要になったら別APIとして検討する
異常系BOT復旧の追加検証: timeout、短い転送、CSW tag mismatch、invalid CSW、phase error、stall、disconnect中のread/write
DFMiniPlayer内蔵SDカードなど非準拠MSCで、自動フォールバックと`setSkipSyncCache(true)` / `skipSyncCache = true` の実機効果を確認する
reset recovery中のEP0 STALLでHCD assertに落ちないよう、reset/clear halt失敗時の扱いとリトライ抑制を追加で見直す

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
