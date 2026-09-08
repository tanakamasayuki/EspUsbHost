# チャンネル数チェック

状態:
現状把握APIは実装済み。`endpointChannelCount()`、`managedEndpointCount()`、`ep0ChannelCount()`、`hubEndpointChannelCount()`、`estimatedHcdChannelCount()`、`maxEndpointChannelCount()`を公開し、claim成功/失敗ログとdevice info表示にも反映済み。

`maxEndpointChannelCount()`が8固定だったのを修正済み（2.8.0）。channel数はcontrollerごとの値で、`soc/usb_dwc_cfg.h`ではS2/S3が8、ESP32-P4のfull-speed controllerが8、high-speed controllerが16。選択中のポートを見るようにした。

CDCについてはカウントの正確性が実機で確認できた（2.8.0、CDC multi-port対応時）。P4のfull-speed host + 3ポートCDCデバイスで、descriptor上のendpointは9本（bulk 6 + notification interrupt 3）だが、claimするのはdata interfaceだけなのでbulk 6 + EP0 1 = 8本中7本。推定カウントどおり3ポートが上がった。control interfaceも claim していた頃の3 ch/ポート換算なら10本必要で3ポート目は上がらない。

残作業:
CDC以外でカウントの正確性を確認する
Hubのみ、HID、MSC、MIDI、Audioを順に追加
推定カウントとESP-IDFの失敗ログを比較
上限近くで失敗するのか、もっと早いのかを見る（S2/S3/P4-FSは8、P4-HSは16）

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

# USB serial (CDC) マルチポート

状態:
2.8.0で1デバイス複数CDC ACMポートに対応済み。ポート単位の`SerialPortState`、CDC Union functional descriptor（`bSlaveInterface0`）によるcontrol↔data対応付け、IN endpointアドレスによる受信振り分け、シリアル系APIへの`port`引数、`EspUsbHostCdcSerial::setPort()`、`serialPortCount()` / `getSerialPortInfo()`。

claimするのはdata interfaceだけで、control interfaceはEP0経由で駆動する（`SET_LINE_CODING` / `SET_CONTROL_LINE_STATE`は`wIndex`にinterfaceを入れれば届く。ESP-IDFがclaimを要求するのはendpointと通信する場合だけ）。これで1ポート2 channelになり、`ESP_USB_HOST_MAX_SERIAL_PORTS`はcontrollerの上限に合わせてP4が7、それ以外が3の固定値。設定不可にした。

非同期CDC OUTキューは`serialWriteQueueBegin()`でヒープ確保する形に変更（1ポート112→32バイト）。これにより7ポート持っても静的RAMは単一ポート時代と同等以下。`tests/peer/usb_serial`に`test_usb_serial_write_queue`と`test_usb_serial_write_queue_cycles`を追加した。それまでこのキューは自動テストが一切通っておらず、実利用は手動の`usb_display_turing`だけだった。

残作業:
`configureCdcAcm()`の呼び出しを絞る。現在はcontrol interfaceのdescriptorを見た時点で無条件に`SET_LINE_CODING`を出しているが、claimしていた頃は「claim成功後」という暗黙のゲートがあった。ポートが使える状態（data interfaceとendpointが揃った）になってから送るほうが素直で、claimできなかったポートへの無駄なEP0転送も消える。列挙途中で消えるデバイスへ転送を出す窓もわずかに狭まる
7ポートのデバイスを実機で確認する。現状の実機確認はP4 full-speed hostでの3ポートまでで、7ポートはP4 high-speed hostでしか到達できない

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
- NTB 入力サイズの交渉: `GET_NTB_PARAMETERS` を読み、`SET_NTB_INPUT_SIZE`（`bmNetworkCapabilities` bit 3）で device を制限、非対応なら device の最大値へバッファを合わせる（上限 `ESP_USB_HOST_NETWORK_NTB_IN_LIMIT`）。決定値は bulk IN の MPS の倍数。
- 診断: `networkStats()`（`ntbInSize` / `rxOversized` 込み）。peer test `tests/peer/usb_ncm`（DHCP + HTTP GET）と `tests/peer/usb_ncm_throughput`（両方向 soak、複数 datagram を 1 NTB にまとめる device 相手の回帰）。

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

UAC1 / UAC2 Feature Unit の Mute / Volume GET/SET は追加済みです。
Mixer / Selector / Processing Unit や、Feature UnitのBass/Treble/AGCなどは未対応です。
Clock Source / UAC2 対応（対応済み）

UAC1 / UAC2 の Type I に対応しました。版はbInterfaceProtocolとbcdADCから判定し、
UAC2ではAS_GENERALのbNrChannels、Clock Source entity（bTerminalLinkから解決、
サンプルレートは`SAM_FREQ`の`RANGE`リクエストで取得）、4バイト・2ビットの
`bmaControls`、volumeの`RANGE`、`CUR`のrequest code、非同期playbackの
explicit feedback endpointの除外を扱います。
`tests/unit/audio_uac`（ホスト単体）と`tests/peer/usb_audio_uac2`
（`EspUsbDevice`のUAC2 peer）で確認済み。
残りは Clock Selector / Clock Multiplier です。ESP32-S3/S2はfull-speed専用なので、
full-speed configurationを持たないUAC2機器はそもそも列挙できません。
feedback endpointによるOUTレート追従（対応済み）

explicit feedback IN endpointをplayback中にポーリングし、報告されたレートで
OUTパケットを刻むようにしました（frame accumulatorが `audioSampleRate` ではなく
`audioOutputPacingRate()` を使う）。ペイロードはUSB 2.0 5.12.4.2どおり、3バイトは
10.14、4バイトは16.16として読み、high speedはmicroframeあたりとして換算します。
ネゴシエート済みレートの±12.5%外はLinuxの`snd_usb_audio`と同じ窓で棄却します。
参照用API: `audioOutputHasFeedback()` / `audioOutputFeedbackRate()` /
`audioOutputFeedbackUpdates()` / `audioOutputFeedbackRejects()` / `audioOutputRate()`。
`tests/unit/audio_uac`（デコードと窓の単体テスト）と`tests/peer/usb_audio_uac2`の
`f`コマンド（実機で48 kHz近傍への追従を確認）でテスト済み。

残り: 実機の非同期DAC/audio IFでの長時間確認。peerのfeedbackはTinyUSBの
FIFO_COUNT方式なので、市販機器の挙動そのままではありません。
フォーマット選択の強化（対応済み）

`audioInputStart()` / `audioOutputStart()` の引数に `0`（指定なし）を許可し、
descriptor順の先頭一致ではなく `espUsbHostAudioStreamScore()` のスコアで選ぶように
しました。共通実装は `espUsbHostSelectAudioStreamForFormat()`。
またフォーマットをalt settingで分けるデバイスへの対応として、claimしていないaltの
endpointを確保しないようにし（従来はaltごとにendpoint slotとisoc transferを浪費し、
OUT側は最後に解析したaltでフォーマットを上書きしていた）、そのaltは
`startable = false` のフォーマット情報として報告します。
`tests/unit/audio_uac` に選択ロジックのテスト、`tests/peer/usb_audio_uac2` に
`(0, 0, 0)` 起動の実機確認を追加。

残り: 開始時に別のaltへ切り替える処理（interfaceのrelease + 再claimが必要）。
1 streamに複数フォーマットを提示できるpeerが無いため未検証で出せない。
EspUsbDeviceは1 streamにつき1フォーマットのみ（descriptor writerが
formatCount != 1 を拒否）なので、peer側の対応も必要。

実機互換性テスト

USBスピーカー、USBマイク、USBオーディオIFでの手動確認。
特に alt setting、サンプルレート、最大パケットサイズ、同期方式で差が出やすいです。
Audio Input サンプルの実受信確認

Input サンプルはビルドと情報表示までは整えました。
実USBマイクで audio: ... bytes_per_sec=... が継続して出るか確認したいです。
ドキュメントの対応範囲明記（対応済み）

README.md / README.ja.md のUSB audio API節末尾に「Audioの対応範囲」小節を追加。
対応: Isochronous IN/OUT、UAC1 / UAC2 Type Iフォーマット解析・サンプルレート選択、Feature UnitのMute/Volume、
UAC2のClock Source。
非対応: Clock Selector / Clock Multiplier、Mixer / Selector / Processing Unit、Mute/Volume以外のFeature Unit control。
OUT/IN は UAC1が標準Arduino `USBAudioCard`、UAC2が `EspUsbDevice` peer で送受信確認済み、
実USBマイク・オーディオIFの確認は継続、full-speed専用という制約あり、と明記。

# P4対応

USB HSだとUSB HUBが実質使えない
FS側だと使える
HS物理ポートを `HCFG.FSLSSUPP` でFS専用にする調査、実験用config、probeは
[`docs/p4-hs-port-fs-only-hub.ja.md`](docs/p4-hs-port-fs-only-hub.ja.md) にまとめた。
実機matrixとcore error recovery後のbit再設定が残作業。
ループバックテストでデバイスとHOST両方でどこまで動いているか個別確認をする

# device lifecycle / MIDI の listener API

状態:
2.4.0で入力系6種（keyboard / keyboard state / mouse / consumer control / system control / gamepad）にlistener APIを追加済み。`onDeviceConnected` / `onDeviceDisconnected` / `onMidiMessage`は単一slotのまま。
統合ライブラリESP32KeyBridgeは、この3つを共有するために自前の共有ハブ（約150行）を持っている。ハブが使う6フックのうち4つは2.4.0のlistenerで置き換え可能になっており、残り2つ（`onDeviceDisconnected`、`onMidiMessage`）のためだけにハブ全体が残っている。

仕様案: docs/lifecycle-listener-proposal.ja.md

対応済み（2.6.0）:
`addDeviceConnectedListener()` / `addDeviceDisconnectedListener()` / `addMidiMessageListener()` を2.4.0と同じ契約で追加した
listener容量は案Aを採用。lifecycle専用の `ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS`（既定8、`EspUsbHost::MaxLifecycleListeners`）を分けた
peer test項目を `tests/peer/usb_midi` に追加した。接続eventはDUTの `end()`+`begin()` による再列挙、切断→再接続はpeerの再起動で作る
`onAudioOutputRequest`は応答系なので単一slotのまま。非const参照＝応答系＝単一slot、const参照＝観測系＝listener化可、という判定基準を仕様案に残した

残作業:
実機でのpeer test実行（`tests/peer/usb_midi`、実行済み。8件すべてPASS。
UAC2対応の回帰確認と同時に実施した）
ESP32KeyBridge側の共有ハブ `EspUsbHostHub`（約150行）と `forStack()` singleton索引の削除、examplesの `sketch.yaml` のEspUsbHostバージョン更新
リリース時に footprint matrix を再生成する（listener slotの追加でRAMが数百バイト増える）

# テスト終了時の後始末（pytest-embedded-arduino-cli）

状態:
テストが終わってもDUT/peerがUSBデバイスやBLE advertiserとして動き続ける問題への対応を、pytest-embedded-arduino-cli側で設計中（2026-09-08時点で設計合意済み・実装前）。EspUsbHost / EspUsbDevice / EspBle 横断の話で、BLEは電波がリグの外へ出るため要件が強い。

設計の結論:
START / RECOVER / STOP の3予約コマンド。コマンドごとに独立してopt-inで、iniで command+reply を設定したときだけ有効、未設定なら何も送らない。デフォルト文字列は持たない（peerスケッチは`Serial.read()`の1文字ディスパッチが多く、既定トークンを送ると実コマンドが発火する。複数文字トークンは1文字ずつ分解されるためさらに危険）。推奨値はSTART 0x01 / RECOVER 0x18 / STOP 0x04 と行終端。
応答は「コマンドを受信した」ではなく「目的の状態に到達した」を意味する。RECOVERの目的状態は「そのスケッチ自身のboot state」なので、STARTでゲートするスケッチではidle、しないスケッチでは稼働中になる。
STARTはフィクスチャ接続後・テスト本体前に、peers（名前順）→ primary の順で送る。この順序は保証される。
STARTのack待ちは既定15秒でno-ackはsetup ERROR（knobなし）。RECOVER/STOPは既定2秒。

このリポジトリでの採用方針:
START = `usb.begin()`。ただし`begin()`は非同期でFreeRTOSタスクを起動して即座に返るため、返った時点でackするとテスト本体がデバイス不在で走る。`onDeviceConnected`または`serialReady()`まで ack を遅延する
RECOVER = STOP = `usb.end()`（STARTがあるのでboot stateはidle）
`x`（`end()`+`begin()`）はRECOVERではなくテスト動作として存続する。`test_usb_serial_end_rebegin_with_device_open`が意図的に検証している
移行手順は、全peerテストモジュールにno-opの`arduino_cli_dut_start`を置く → iniで有効化 → モジュール単位で変換、の順。全部赤にしてから直す方式は取らない。CIとデバイス側のpeer/loopbackが常時緑である前提で回っているため、赤の期間に入る他の変更の回帰が見えなくなる

残作業:
plugin側の実装待ち。来たら`tests/peer/usb_serial`に配線して実機確認する
`conftest.py`の`_KNOWN_SERIAL_FINDINGS`統合。`USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE`が同一理由で6件登録されており、テストが増えるたびに増える（2.8.0のリリース前テストでも7件目として`hid_keyboard_composite`に出た）。原因はホストが先に書き込まれて`usb.begin()`まで走ったあとにpeerが書き込まれ、esptoolのリセットのたびにpeerの内蔵USB-Serial/JTAG（303a:1001）が出入りするのをホストが見ること。書き込み順は他プロジェクトへの影響があり変更しない方針。STARTで`usb.begin()`をゲートすればホストはその窓でUSBホストになっていないためノイズが発生源で消えるので、統合はSTART採用後にやるほうが無駄がない
全40テストスケッチが`Serial.read()`の未知バイトを無視することは確認済み（catch-all elseなし）。予約バイトを送っても安全
