# チャンネル数チェック

状態:
現状把握APIは実装済み。`endpointChannelCount()`、`managedEndpointCount()`、`ep0ChannelCount()`、`hubEndpointChannelCount()`、`estimatedHcdChannelCount()`、`maxEndpointChannelCount()`を公開し、claim成功/失敗ログとdevice info表示にも反映済み。

残作業:
実験してカウントの正確性を確認する
Hubのみ、HID、CDC、MSC、MIDI、Audioなどを順に追加
推定カウントとESP-IDFの失敗ログを比較
8 に近いところで失敗するのか、もっと早いのかを見る

どのタイミングで制御すべきか検討する
interface_claim 前で止めるべきか
device単位で除外すべきか
endpoint単位の抑制に意味があるか
Audioのように遅延claimした方がよいものがあるか確認する

制御用コールバックを追加する
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

# USB Network

状態:
⚠️ **本物のUSB NICではまだ使えない**。実機NIC (AX88179A 等) は CDC-NCM/ECM を active でない configuration に持ち、その選択が現行 Arduino-ESP32 core (3.3.10) では不可 (`CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK` が無効) なため。有効化PRはマージ済みで、**次回 Arduino-ESP32 リリース以降に実機NIC対応予定**。それまでは、兄弟ライブラリ EspUsbDevice の NCM device (active config が CDC-NCM) に対してのみ動作する。

実装済み (EspUsbDevice NCM device 相手に実機検証済み):
- `getNetworkInterfaces()`／`tests/manual/usb_network_descriptor`: 全configuration横断のCDC-ECM/NCM候補検出。AX88179A で config1=vendor / config2=NCM / config3=ECM を確認。
- `networkOpen()`／`networkClose()`／`networkReady()`: active configuration 内の CDC-ECM/NCM 候補に限定して claim。
- notification (interrupt IN) / bulk IN・OUT の開始。
- CDC-NCM の NTH16/NDP16 parse・build（複数 bulk-IN 完了にまたがる NTB 再アセンブル込み、1 NTB 1 datagram 送信）。
- raw frame API: `onNetworkFrame()` / `networkWriteFrame()` / `networkReadFrame()` / `networkLinkUp()`。
- lwIP (`esp_netif`) 統合: `networkAttachNetif()` / `networkDetachNetif()` / `networkLocalIP()`、DHCP client / static IP(+DNS)、切断時 detach。
- host netif MAC は CDC `iMACAddress` を読んでそのまま採用（未提供時はローカル管理MACにフォールバック）。
- 診断: `networkStats()`。peer test `tests/peer/usb_ncm`（DHCP + HTTP GET）。

方針:
特定VID/PID専用ではなく、CDC-NCM、CDC-ECMの順に標準クラスを優先する。
vendor-specific Ethernet protocolは標準クラスで不足が出た場合に検討する。
lwIP統合までを見据え、USB class driver層、raw Ethernet frame層、lwIP netif層、routing/NAT層を分ける。

残作業:
Arduino-ESP32次回リリース以降の `enum_filter_cb` 有効環境で、汎用configuration選択（active でない configuration の CDC-NCM/ECM を開く）を実装する ← 実機NIC対応の本丸
CDC-ECM raw Ethernet frame RX/TX を実機で確認する（実装は NCM と共通経路、ECM 実機は未検証）
NAT/NAPT有効buildでのAPIと、非対応buildでの明確な失敗扱いを設計する
Wi-Fi STA/APとUSB NICの組み合わせ例とmanual testを追加する
複数 USB NIC 同時の lwIP 統合

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
→標準Arduino `USBAudioCard` をSPK+MIC構成にして、Host → Device のAudio OUTとDevice → HostのAudio INを同一peerテストで確認済み。
単発の `USBAudioCard.write()` はisochronous INのタイミングと合わず取りこぼすことがあるため、テストでは1ms間隔の短いburst送信で確認している。
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
ドキュメントの対応範囲明記（対応済み）

README.md / README.ja.md のUSB audio API節末尾に「Audioの対応範囲」小節を追加。
対応: Isochronous IN/OUT、UAC1 Type Iフォーマット解析・サンプルレート選択、Feature UnitのMute/Volume。
非対応: UAC2 / Clock Source / Clock Selector、Mixer / Selector / Processing Unit、Mute/Volume以外のFeature Unit control。
OUT/IN は標準Arduino `USBAudioCard` のpeerで送受信確認済み、実USBマイク・オーディオIFの確認は継続、と明記。

# P4対応

USB HSだとUSB HUBが実質使えない
FS側だと使える
ループバックテストでデバイスとHOST両方でどこまで動いているか個別確認をする
