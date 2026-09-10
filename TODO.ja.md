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

# テスト作法の整備（pytest-embedded-arduino-cli のガイド準拠）

状態:
テストが終わってもDUT/peerがUSBデバイスとして動き続ける問題への対応。EspUsbHost / EspUsbDevice / EspBle 横断の話で、BLEは電波がリグの外へ出るため要件が強い。

2026-09-08にplugin側でSTART / RECOVER / STOPの予約コマンド機能（1.5.0 / 1.5.1）が入り、EspUsbHost側も全85スケッチを変換して実機検証まで進めたが、**2026-09-09にこの機能は撤回された**。1.5.0と1.5.1はPyPIから削除済み。「conftestのfixtureで同じことが全部できる」と実測で分かったため。plugin 1.6.0は自発的にバイトを送らない。EspUsbHost側の変換もリセット済みで、残っているのはpluginピンの1.6.0のみ。

代わりにテスト作法のガイド3本が用意された。日英そろっていてREADMEからリンクされている。場所は `/home/mt/dev/pytest-embedded-arduino-cli`。
- `TESTING_BASICS.md` — テストとは何か、session / module / test、台数別、ディレクトリ構成、`.env`、起動待ち
- `TESTING_ADVANCED.md` — 収集規則、marker、conftest、設定の優先順位、expectの落とし穴、中断時の後片付け、ボード状態の観測、peerテストの原則
- `TESTING_EXAMPLES.md` — 実例集（EspUsbHostは未収録。適合してから再検討する）

方針:
ガイドの「起動時に外界へ影響を与えるな、テストが指示したときに開始せよ」に従う。撤回されたのは送り手だけで、スケッチ側をゲート化する形自体はガイドの推奨形。送り手はテストファイル内のautouse fixture（ガイドはconftestを最後の手段としている）。
予約バイトは不要になった。全スケッチ共通の契約ではなくモジュール単位になったので、そのスケッチのコマンド体系だけ見ればよく、ガイドの慣習（ホストからは小文字、デバイスからは大文字）に従える。

実測で分かっていること（撤回前の検証で得た。作り直しでもそのまま効く）:
ゲートが必要なホストスケッチは `tests/peer/` の21本。`usb.begin()` を `setup()` から出す。
ready判定に `deviceCount()` は使えない。`inUse` は device 確保時に立ち、interface の claim は `parseConfigDescriptor` の後なので、列挙完了より早く真になる。`onDeviceConnected` は claim の後に発火するので、`addDeviceConnectedListener()` で立てたフラグを使う。`end()` はlistenerを消さないのでbegin/endを跨いで有効。
**有効化コマンドの応答待ちが、スケッチの起動時出力を食う。** fixtureが応答を `expect()` で待つと、pexpectの読み取り位置がそこを通過し、応答より前に出力された行はテスト本体から永久に見えない。21モジュール中8本で踏んだ（`hid_logic` は `TEST_BEGIN` を出力済みなのにタイムアウト、`usb_audio` は `AUDIO_IN_READY` / `AUDIO_STREAM` を含む12行が消えた）。起動時出力は応答の**後**に出す必要がある。
**応答をいつ返すかは両立しない要求のトレードオフ。** 早く返すと列挙前にテスト本体が走ってレースする（`usb_midi` が2→9失敗に悪化。peerが送ったMIDIノートを、まだinterfaceをclaimしていないホストが取りこぼす）。遅く返すと上記のとおり起動時出力が食われる。`usb_midi` は両方必要で、唯一成立する形は「使用可能になるまで応答を遅らせ、応答後に起動時出力を再送出する」。
この2点はplugin機能の性質ではなくガイドが推奨するパターン自体の性質なので、送り手がfixtureになっても同じく当てはまる。指摘済みで `TESTING_ADVANCED` に反映されている。

ステートレス規則への適合（2026-09-09 実測済み）:
ガイドは「全テストが単体で通ること、1 module 1 テストを基本、複数テストはステートレスな場合だけ」と定めている。
複数テストを持つ13モジュール・計67テストを1件ずつ個別のpytestプロセスで実行した結果、**67/67 PASS**。EspUsbHostは「単体で通る」規則には**既に適合している**。EspUsbDevice側は同じ監査で109件中13件が落ちており、スイートの書き方の差が出ている。
そのため統合や分割は不要。当初想定していた大規模な作り直しは要らない。

**ただし並び替えには耐えない。** 「単体で通る」は必要条件であって十分条件ではない。順序依存には向きが2つあり、単独実行監査はどちらも検出できない。
- 前のテストが張った状態にただ乗りする → 単独で落ちる／通常実行で通る（EspUsbDevice側とEspBle側で観測）
- 起動時に一度しか出ない出力を待つ → 単独で通る／通常実行でも通るが、**前にテストを追加すると落ちる**（EspUsbHostはこちら）

EspUsbHostで該当するのは、`HOST_CONNECTED` など起動時の一度きりの出力をexpectしているテスト。いずれもモジュールの1番目に置かれているので現状は通っている。2番目に動かして実測した結果:
    hid_keyboard_composite  BREAKS   test_hid_keyboard_composite_led
    hid_keyboard_nkro       BREAKS   test_hid_keyboard_nkro_detected
    usb_ncm                 BREAKS   test_usb_ncm_enumeration
    usb_midi                ok       test_usb_midi_port_info
    usb_vendor              ok       test_usb_vendor_enumeration
usb_midiとusb_vendorが通るのは、前に置いたテストが再列挙を起こして`HOST_CONNECTED`をもう一度出させているため。依存が無いわけではない。

並び替え耐性の是正は完了（2026-09-10）。`hid_keyboard_composite` は冗長な `HOST_CONNECTED` 待ちを削除、`hid_keyboard_nkro` は `i` をポーリングする待ちに変更、`usb_ncm` は stop → start → ready待ち → assert の形（`claim_attempts=0 claimed=0 managed=0` はattach前にしか成立しないので、最初に走ることに頼るのをやめて、その状態を明示的に作る）、`usb_msc` の `request_sense` は保留中のsenseを吐かせてから `t` を発行する形に変更。逆順パス0件、通常順74 passed。
モジュール単位の組み直しも完了。21スケッチをゲート化し、21テストファイルにautouse fixtureを置いた。

`usb_ncm` は一度「統合」で済ませたが、これは順序をテスト内に隠しただけで依存は残っていた。当時のdocstringに「ホストを戻すコマンドが存在しない」と書いたが、**その前提はゲート化した時点で偽になっていた**。`H`/`G` がまさに戻すコマンドで、自分で追加しておきながら依存除去の道具として使えることに気づいていなかった。`claimAttempted`/`claimed` は `getInterfaces(deviceAddress, ...)` が返すデバイス単位のフラグで、`end()` でデバイスごと消えて `begin()` の再列挙で作り直される。attach状態も `onDeviceDisconnected` が落とす。
**ゲート化は順序依存の検出可能性（逆順チェック）だけでなく、修正可能性も与える。** 「起動直後にしか成立しない状態をアサートする」形は設計の誤りだが、ゲートが無い環境では直す手段自体が存在しない。統合に逃げる前に、手段を作れないかを確認する。

ゲート化の費用（2026-09-10 実測。同一リグ・同一日）:
| 条件 | フル74テスト | usb_msc(20テスト)単体 |
|---|---|---|
| A: 起動時に host begin、テスト毎の準備なし | 12m33s | 40.28s |
| B: テスト毎に stop/start（USB完全再列挙） | 21m49s | 437.88s (7m17s) |
| C: モジュール毎に1回だけ start/stop | 13m20s | - |
3条件とも74 passed。監査はBとCが147ログ中0件、Aは1件。
A→Bは+9m16s(+74%)だがA→Cは**+47s(+6.3%)**。usb_msc単体では19回の追加再列挙が+398s、**約21s/テスト**。
**ゲートの費用と、ゲートをテスト毎に払う費用はまったく別物。** Bの+9m16sのうち実際のゲートの費用はCの+47sだけで、残り8分半は「窓が1回しか開かないのに毎テスト閉め直していた」分。費用の94%が機能ではなく置き場所から来ていた。
Cにすると6モジュールが壊れると予想していたが**壊れたのは0本**。効いていたのは再列挙による出力の再生ではなく、fixtureを状態問い合わせ型にしてあったこと。順序依存の疑いはコードを読んでも判定できず、条件を変えて実機で走らせて初めて分かる（分類をpoll/connectの両方向で読み違えている: `custom_hid`, `usb_midi`）。
**分割の限界費用はテスト数ではなくテスト毎fixtureの実測時間で決まる。** 追加コスト = (テスト数-1) × テスト毎fixtureの実測時間。EspUsbDevice側の同じ測定は0.26s/テストで、あちらはper-test teardownが無い構成。同じ「テストを1件足す」が80倍違う。
したがって「複数テストは遅いから1テストに統合する」はBのようなfixtureを持つ場合にだけ成り立ち、そのときの正しい直し方は統合ではなく**準備をテスト毎の経路から外に出すこと**。統合してもモジュールが増えれば同じ費用が戻る。
実装上、fixture自体をmodule scopeにはできない（`dut`/`peers` がfunction scopeなので `ScopeMismatch`）。採った形は、fixtureはfunction scopeのまま、開始コマンドをスケッチ側で冪等にする（`if (hostStarted) return;`）。Python側に状態を持たなくて済む。停止はモジュール最後のテストでのみ行う。
ゲートが閉じる窓はpeerの書き込み中で、これはモジュールに1回しか起きないので、モジュールに1回で足りる。

ゲート自体の効果: ゲート無し／ゲートが無効だった3ランでEnqueue URBを1件・2件観測、ゲートが効いた2連続ランでは147ログ×2で0件。
autouse fixtureは `peers` を要求しないとpeerの書き込みより先に走ってゲートが無効化される。`peers` を要求しているのは値のためではなく**順序のため**だけ。一度これで無効化され、ログに `HOST_STATE running devices=1` → `Enqueue URB error` が残った。

`_KNOWN_SERIAL_FINDINGS` の統合は完了。同一理由・同一パターンの5件を `*peer/*` の1件にまとめた。マッチは最初に当たったルールで止まるので、**固有ルールは一般ルールより前**に置く（`usb_midi` の意図的リブートのエントリを食わせるところだった）。

上記はすべて検証済み（2026-09-10）。`usb_ncm` PASS、`usb_ncm_throughput` PASS、`usb_vendor` 逆順 PASS。

# テスト計画のゼロベース再設計（2026-09-10）

ガイドが「1モジュール1テスト」を推奨から**規則**に格上げしたのを受けて、`peer/` を再構成した。

**鍵になった事実: 規則への適合は統合すれば無料どころか速い。** 51倍という数字は「モジュールに分ける」費用で、
EspUsbHost は既にスケッチ1つ＝モジュール1つなので、統合してもモジュール数は変わらない。実測:

    usb_msc  20テスト/1モジュール  46.78s
    usb_msc   1テスト/1モジュール  31.02s   ← テスト単位の固定費 約0.83s × 19回分が消える

74テスト/21モジュール → **22テスト/21モジュール**。`usb_midi` だけ2テスト。

構成:
複数テストだったものは1テスト内の**チェック**（入れ子関数）にした。`tests/conftest.py` の `run_checks`
フィクスチャが全部実行して失敗したものを全部報告するので、1件壊れても残りは検証されたままになる。
チェック名は関数名から取るので、名前が本体と食い違うことがない。

統合しなかった例外は1件。`usb_midi` の `lifecycle_listeners_on_peer_reboot` は `_KNOWN_SERIAL_FINDINGS` に
ノードID単位の許可エントリを持つ。統合すると許可がモジュール全体に広がり、許すべきでない場所で
同じ行が見逃される（ガイドが名指ししている事例）。

**統合は症状を消すだけで原因は残る**（順序依存がテストの内側に移る）ので、監査も内側に移した。
`ESPUSBHOST_REVERSE_CHECKS=1 pytest peer/` で全モジュールのチェックが逆順に走る。アップロードの追加ゼロ。

その他:
`pyproject.toml` の name が `demo-add-library-tests`（コピー元の名残）→ `espusbhost-tests`
`testpaths = ["unit", "peer"]` を宣言。既定実行が manual/probe の命名の偶然に依存しないようにした
`expect` の行末固定を30件。**AST で取り直したら25件あった**（1行 grep では4件しか見えていなかった）。
うち `usb_serial` の `ready=([0-9]+)` と `usb_ncm_throughput` の `ntbIn=(\d+)` は切れうる値を実際に使っていた。
`manual/usb_display_throughput` の `DISPLAY_TUNE_READY (\d+)x(\d+)` だけは行が `mode=%s` と続くので、
行末ではなく後続リテラルで固定した。**一括で行末固定をかける前に、スケッチ側の書式が本当にそこで終わるか確認する。**

`unit/` も整理した。12ファイルが同じ28行の g++ 定型（コンパイル→実行→終了コード確認）を複製していたので、
`unit/conftest.py` の `build_and_run` フィクスチャにまとめた。定型だけの9ファイルは 52〜60行 → 27〜35行。
副次的に `-Werror` が全ファイルで揃った（`keymap` / `audio_uac` / `midi_cable` は付いていなかったが、3つとも警告なしで通る）。
`keymap` の `-funsigned-char` は Xtensa が unsigned、host が signed で、テーブルがそれで添字付けされるため。README に理由を明記。

実機結果（2026-09-10）:
    フルテスト（unit 12 + peer 22）  34 passed  18m19s
    チェック列を逆順にした監査       22 passed  15m36s

**この時間は比較に使えない。** 裏で実機なしのビルドが並行して流れていたため。内訳がそれを示している:
コンパイル支配的なモジュール（`hid_*`、小さいスケッチ）が +20〜29s、実行時間支配的なもの（`usb_msc`、`usb_ncm`）が +0.7〜4.7s。
**私が一切変更していない `hid_logic` が +15.5s** なので、再構成が原因ではない。`.bin` も 328/369 が同時間帯に再生成されていた。
競合の影響を受けにくい2点だけが統合の効果として読める:
    usb_msc  20テスト 48.6s → 1テスト 49.3s（横ばい）
    usb_midi の2件目 `test_usb_midi` が 2.78s（モジュール固定費は1件目が払い、2件目はテスト単位の費用だけ）
正確な再測定はマシンが空いてから。

`usb_ncm` をさらに強い形にした（**実機未検証。リグが空いたら流すこと**）。
EspBle 側が独立に定式化した「初期状態を assert する case は、同じ case の中で先に壊してから確かめる」に合わせた。
順序は attach → 機能確認 → **カウンタが0でないことを確認** → stop/start → pristine を assert。
先に pristine を assert する形（従来）は2重に弱い。前に何か置くと偽陽性で落ちるうえ、
**カウンタが非ゼロになりうることをテスト内が何も示していない**ので、常に0を報告するスケッチでも通ってしまう。
私はプラグイン側に「あのアサーションは落ちることができなかった」と送ったが、これは言い過ぎだった。
製品側が退行して discovery が claim すればブート直後でも落ちる。実際に起きていたのは順序依存で、
空アサーションの側面は「報告経路が壊れていても検出できない」という別の話。訂正済み。

空アサーション（報告経路の故障に気づけないアサーション）のスイート全体監査（2026-09-10、実機不要）:
「0を主張していて、同一モジュール内で非ゼロを一度も観測していないフィールド」を AST で洗い出すと65件出るが、
ほぼ誤検知。3つの理由で消える。
1. **開始時の衛生確認であって主張ではない**（EspBle の区別）。`devices_before=0` など。むしろ環境が満たしているのが正常
2. **同一モジュールの別チェックが動きを示している**。`ready=0` に対する `ready=1`、`devices=0` に対する `devices=[1-9]`
   （後者は `_poll_state` が変数でパターンを渡すので AST スキャンから漏れていた。静的監査の限界）
3. **1行の中の冗長な符号化**。`HOST_END installed=0 clients=-1 devices=-1` は `usb_host_lib_info()` の1つの結果を
   3通りに出しているので、無条件に0を返す報告経路は3つの相関ごと偽装しないと通らない。
   `QUEUE_STATS submitted=3 completed=3 errors=0` も同じで、`errors` だけ死んでいても submitted/completed が合わない

**真の該当は `usb_ncm` の1件だけだった。** 3フィールドとも0で、どれも非ゼロを観測しておらず、しかもそれが主張そのもの。
修正済み（未検証）。

逆順監査そのものが空振りしうる点に対処（2026-09-10）。
反転が壊れると `ESPUSBHOST_REVERSE_CHECKS=1` は静かに2回目の順方向実行になり、
全モジュールが通り続けて監査は成功を報告する。**逆順実行が通ったことは反転している証拠にならない。**
`tests/harness/test_run_checks.py` を追加し、順序を直接アサートする形にした（実機・スケッチ不要、0.1秒）。
変異検査で落ちることも確認済み（`order.reverse()` を no-op にする / `check()` を try-except で包む、どちらも検出）。
一般化: 逆順検査・単独実行監査・シリアルログ監査はいずれも「壊れると静かに全部通る」性質を持つ。
見分け方は**その検査が失敗を報告した実例があるか**。シリアルログ監査には実例があるが、逆順監査には無かった。

許可リストの自己検査を追加（`tests/harness/test_known_findings.py`、実機不要）。
EspUsbDevice が110→29テストの統合で踏んだ回帰への対策。**ノードIDで引く許可エントリは、テストのリネームで黙って外れる。**
テストは落ちず、それまで許可されていた行が「未知の異常」として報告され始めるだけなので気づきにくい。
検査は2つ。各エントリが実在するテストに1件以上一致すること、および first-match で止まるので固有エントリが一般エントリより前にあること。
ノードIDは pytest に訊かずツリーを AST で読んで集める（許可リストは `manual/` と `probe/` も対象で、そこは既定実行に入らないため）。
**空集合の落とし穴**: 一致しないエントリは空集合で、空集合はあらゆる集合の部分集合なので、順序検査が無条件に発火する。
リネーム時に原因と無関係な「順序違反」が出てしまうので、空のルールは順序検査ではスキップして孤児検査に委ねる。
EspUsbDevice が先に書き上げて先に踏んだ穴で、こちらは誤報を見る前に塞げた。
変異検査で両方が落ちること、かつ**それぞれ正しい1件だけ**が落ちることを確認済み。

`tests/harness/` を層として追加。逆順監査そのものが空振りしうる問題への対策も含む。
`.env` なしで `harness/` と `unit/` が通ることを確認（18 passed）。sketch.yaml と .ino がゼロなので host-only が保たれる。
EspUsbDevice は CI で `unit/` を回していなかったため実機依存が7モジュール混入していた。**CI が回しているから host-only が保たれる**という因果。
こちらは `unit-tests.yml` が push ごとに `.env` なしで回しているので守られている。`probe/` と `manual/` には同じ守りが無いが、実機や人が要る層なので割り切る。

`testpaths` は宣言しない結論にした（一度宣言したが撤回）。
EspUsbDevice の指摘: **宣言すると後から足した層が黙って収集されず、総数も変わらないので緑のまま。**
過剰収集は総数が増えて見えるが、収集漏れは見えない。許可リストがリネームで外れるのと同じ構造。
最初は「規模が違うので宣言を残して検査で穴を塞ぐ」としたが、ユーザ判断で撤回。理由が正しい:
**`test_` 前置を外すというルールが既に仕組みなので、宣言は二重化。** しかも間違えて `test_` 付きで
`manual/` に足したとき、宣言があると黙って収集されずミスが隠れる。収集されて気づく方が良い。
実測: 宣言なしの素の `pytest` は harness/unit/peer の40件ちょうどを集める（宣言時と同一）。
`pytest manual/` は0件収集で exit 5、ファイル名指定なら収集される。

クリーンフルテスト（終了条件、2026-09-10）:
1回目 `pytest -q --clean` = **1 failed, 39 passed / 37分06秒**、逆順 22 passed。
2回目 `pytest -q --clean` = **40 passed / 33分29秒**。**再現せず。**

落ちたのは `peer/hid_keyboard/test_hid_keyboard.py::test_hid_keyboard` の
チェック `shift_boot_reports` で、`device.expect_exact("SEND @ 1")` が30秒タイムアウト。
単独実行は3/3 PASS。切り分けで分かったこと:
**peer は `@` を受信して HID レポートを送っている**（DUT が `KEY @` を受信）ので RX 方向は動いていた。
一方 **peer のシリアルログが0バイト**で TX 方向が1行も捕れていない。書き込み完了からテスト実行まで2秒。
機序は特定できず。1回のみの発生で再現しないため、環境の一過性として記録する。

**別件で見つかった構造上の弱点: 20モジュール中13が peer との準備確認を持っていない。**
書き込み直後にいきなり peer へ書いている。準備待ちがあるのは
`hid_keyboard_composite` / `hid_keyboard_nkro` / `usb_audio` / `usb_audio_uac2` /
`usb_ncm` / `usb_ncm_throughput` / `usb_vendor` の7つだけ。
ハンドシェイクの価値は「peer が生きている」ことではなく、**テストが依存する前に読み取り経路が動くことを証明する**点。
これが無いと、モジュール最初の peer 待ちが失敗したとき
「peer が言わなかった」のか「こちらが聞いていなかった」のか区別できない。今回まさにその状態だった。
今回の失敗の原因と断定はできない（peer は受信できていた）ので、独立した改善として保留。

作業事故:
`git checkout <file>` で**未コミットのゲート化作業を消した**（`hid_mouse`）。HEAD にはゲート化前の版しか無く、
checkout はそれで上書きする。会話の記録から復元できたが、以後は `git diff > patch` で退避してから触ること。

時間の比較をするときは、マシンが他の作業をしていないことを先に確かめる。今回は「遅くなった」と報告しかけた。
私が触っていないモジュールも同じだけ遅くなっている、という内訳が無ければ誤った結論を出していた。

Enqueue URB トランジェントについて（決着していない）:
ホストが先に書き込まれて `usb.begin()` まで走った後にpeerが書き込まれ、esptoolのリセットのたびにpeerの内蔵USB-Serial/JTAG（303a:1001）が出入りするのをホストが見る、というのが仮説。書き込み順は他プロジェクトに影響するので変更しない方針。
ログから実測した全ラン横断の件数は、70テスト以上の大規模ラン8回で合計9件。単調減少ではなく、途中と末尾にゼロがある。負荷との相関も一度は疑ったが、デバイス側の再カウントで系列自体が誤りと判明し、支持されなくなった。
**EspUsbHostのスイートには基準率が存在しない。** フルランの記録が1回だけで、そのとき1件（`hid_keyboard_composite_led`）。低い率を検出するには未ゲートの対照ランを複数回積む必要があり、1回30分以上かかる。現在のラン予算では答えが出ない。
ゲート化には、この件と無関係に「テストごとに列挙済み状態から始まる」という価値がある。トランジェントの機序証明を目的に実機時間を積むのは割に合わない。

作業上の注意:
実機監査で全件が落ちたら、テストではなく環境を疑う。2026-09-09に67件を個別実行して全滅させたが、原因はS3ペア2台が`/run/board-identify/by-id/`から消えていたこと（`lsusb`からもCH340が落ちていた。物理を確認して復帰）。全部アップロード失敗だったのに、判定を`grep -qE "^1 passed"`の2値にしていたためsetup ERRORをFAILと誤認し、「単独実行で67件が落ちる」という誤った一覧を出しかけた。判定は passed / failed / error の3値にして、連続ERRORで早期中断する。
`pgrep -f <pattern>` は自分を実行しているシェルのコマンドラインにもマッチするため、終了したジョブを実行中と誤報する。`pgrep -af "[x]xx"` のように書く。EspUsbDevice側がこれで治具を2時間半空押さえした。
`dut.log` には非UTF8バイトが混じることがあり、`grep` がbinary扱いして黙って0件を返す。手動で数えるときは `grep -a`。`tests/conftest.py` の監査fixtureは `read_text(encoding="utf-8", errors="replace")` を使っているのでこの穴はない。
`expect()` の正規表現は行末（`\r?\n`）まで含める。expectはバッファが一致可能になった瞬間に返るので、**末尾が可変長クラスで終わるパターンは行の途中で満たされる**。`NCM_ENUM [^\r\n]*` は `*` がゼロ文字でも成立し、届いている分だけを捕まえて `claim_attempts=0 claimed=0 managed` で切れた。「`.*` ではなく `[^\r\n]*` にして行末で止める」というコメントを書いていたが、それは行の**完成**を保証しない。監査で同型が4件（`usb_ncm`, `usb_msc` の sense drain, `usb_midi` の DISCONNECT_LISTENER, `usb_ncm_throughput` の5パターン。最後のは捕まえた値をそのまま使っていた）。
**構造を変えた直後の失敗が、構造変更のせいとは限らない。** 上のバグはタイミング依存で、行がexpectに対していつ届くかが変わって初めて露出した。fixtureの変更はまさにそれを変える。一瞬「stop/startが状態を壊した」と読みかけたが、DUTのログでは状態は正しく、壊れていたのは読み取り側だった。失敗を見たらまずデバイスのログで被テスト側が本当に間違っているかを確認する。
スケッチのコマンド分岐は `if (command == ...)` だけでなく `switch (command)` の形もある。一括変換で正規表現を使うと後者が漏れ、loop先頭にSerialリーダーを二重に足してテストのコマンドを横取りする事故になる（実際に6本壊した）。分岐チェーンの終端判定は行grepではなくbrace matchingが要る。
