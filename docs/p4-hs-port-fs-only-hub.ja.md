# ESP32-P4 HSポートをfull-speed専用にしたUSB Hub運用の調査

## 結論

ESP32-P4のHS物理ポートを使いながらDWCの `HCFG.FSLSSUPP` を設定すると、
high-speed対応USB 2.0 Hubをfull-speed Hubとして列挙させられる可能性が高い。
Hubのupstream側がfull-speedなら、配下のFS/LSデバイスはsplit transactionや
Transaction Translator (TT) を使わず通常のFS/LS transactionで通信する。そのため、
ESP32-P4 HS Hostで「HS HubとFS/LSデバイスを組み合わせられない」という制限を回避できる。

このリポジトリには仮説を実機確認できる起動経路とprobeを追加した。ただし、現時点では
実機結果がなく、DWC core error後の再設定も未対応なので、APIは
`experimentalForceFullSpeed` としている。通常の設定では従来動作から変化しない。

## 背景と根拠

ESP-IDFはP4について次を明記している。

- P4にはHSとFSの2つのUSB OTG controllerがある。
- HostはHS/FS/LS deviceを扱える。
- External Hub DriverにはTT layerがなく、HS HostへHubを接続した場合、その配下の
  FS/LS deviceをサポートしない。

出典: [ESP-IDF USB Host Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/usb_host.html)

さらにP4のHS DWCは `OTG_SINGLE_POINT=1` で、hardwareのsplit transaction supportが
無い。ソフトウェアにTT requestを追加するだけでは解決できない。

出典: [USB Host Maintainers Notes (DWC_OTG)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/usb_host/usb_host_notes_dwc_otg.html)

一方、P4のDWC LLには `usb_dwc_ll_hcfg_set_fsls_supp_only()` があり、これは
`HCFG.FSLSSUPP=1` を設定する。LinuxのDWC2 Host driverもcontrollerの要求速度が
full/low speedの場合に同じbitを設定する。このため、UTMI HS PHYを使うDWCを
FS/LS-only Hostとして動かすという使い方自体はDWCの想定範囲にある。

- [ESP-IDF P4 DWC LL](https://github.com/espressif/esp-idf/blob/release/v5.5/components/hal/esp32p4/include/hal/usb_dwc_ll.h#L447-L451)
- [Linux DWC2 Host初期化](https://github.com/torvalds/linux/blob/master/drivers/usb/dwc2/hcd.c#L2150-L2157)

期待する接続は次のようになる。

```text
P4 HS connector / UTMI PHY
        │  full-speed (12 Mbit/s)
        ▼
HS対応USB 2.0 Hub（FS deviceとして列挙）
        ├── FS device: 通常のFS transaction
        └── LS device: Hubの通常のLS取り扱い

split transaction: 使用しない
TT:                使用しない
```

代償としてroot bus全体が12 Mbit/sになる。Hub配下のHS対応deviceも、FS fallbackを
実装していればfull-speedで動く。HS専用またはUSB規格外のdeviceは列挙できない可能性がある。

## 現在のEspUsbHostと今回用意した実験経路

従来のP4実装は `usb_host_config_t::peripheral_map` でHS controller (BIT0) と
FS controller (BIT1) を選ぶだけで、HS controllerの最大速度を指定していなかった。
通常の `usb_host_install()` はinstall中にroot portもpower onするため、install後に
`FSLSSUPP` を書く方法では、接続済みHubのreset/HS negotiationと競合する。

今回、`EspUsbHostConfig` に次の実験用設定を追加した。

```cpp
EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
config.experimentalForceFullSpeed = true;
usb.begin(config);
```

指定時の起動順は次のとおり。

1. `usb_host_config_t::root_port_unpowered=true` でHost stackをinstallする。
2. DWC/HAL初期化完了後、root portが停止した状態で
   `usb_dwc_ll_hcfg_set_fsls_supp_only(&USB_DWC_HS)` を呼ぶ。
3. HCFG値と `FSLSSUPP` のreadbackをログへ出す。
4. EspUsbHost clientをregisterする。
5. `usb_host_lib_set_root_port_power(true)` で初めてroot portを有効にする。
6. Hubとdeviceを通常どおり列挙する。

設定はP4のHS port（`DEFAULT`を含む）でのみ受け付ける。P4 FS portや他SoCで指定した場合は
`begin()` を失敗させる。既定値はfalseであり、既存sketchの初期化順は変わらない。

## 実験手順

### 必要なもの

- ESP32-P4 board
- P4のHS OTG connectorへ接続できるUSB 2.0 high-speed対応Hub
- Hub配下へ接続するFS device（HID、touch panel、CDCなど）
- LSも確認する場合はlow-speed keyboard/mouseなど
- Hubとdeviceに十分な電源。消費電流が不明ならself-powered Hubを使う

### 自動probe

`tests/.env` にP4のconsole portを設定する。

```text
TEST_SERIAL_PORT_ESP32P4=/dev/tty...
```

P4のHS portへHub、HubへFS/LS deviceを接続して、`tests/` から実行する。

```sh
uv run --env-file .env pytest probe/p4_hs_fs_hub/p4_hs_fs_hub_probe.py -v -s
```

probeは次をすべて満たすとpassする。

1. `usb.begin()` が成功する。
2. Hubの `EspUsbHostDeviceInfo.speed` が `USB_SPEED_FULL` になる。
3. `parentAddress != 0` のFSまたはLS deviceが1台以上見える。

成功時は次を出す。

```text
PROBE_PASS hub_speed=full downstream_speed=fs_or_ls
```

`DEVICE` 行にはHubの `deviceProtocol` も出す。USB 2.0 Hub classの定義上、FS Hubなら
通常は `protocol=0x00` になるため、速度判定との相互確認に使える。ただしprobeのpass条件は
ESP-IDFが報告する実bus speedを主判定とする。

### 比較用baseline

既存のprobeは速度固定を行わない。

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py -v -s
```

同じHubがbaselineでは `speed=high`、新probeでは `speed=full` になれば、
`FSLSSUPP` の効果をA/B比較できる。問題の組み合わせでbaselineがHost stackをabortさせる場合は、
新probeを先に実行し、baselineは最後に行う。

## 推奨する実機テストmatrix

| 項目 | 構成 | 期待結果 |
|---|---|---|
| 1 | HS port → HS Hubのみ | Hubがfull-speed、protocol 0x00 |
| 2 | HS port → HS Hub → FS HID | HubとHIDを列挙、HID入力が動作 |
| 3 | HS port → HS Hub → 問題が出たFS touch | Hostが落ちず、touchを列挙 |
| 4 | HS port → HS Hub → FS + HS対応device | 両方full-speedで共存 |
| 5 | HS port → HS Hub → LS device | LS deviceを列挙して入力可能 |
| 6 | Hub/deviceの抜き差しを10回 | 毎回同じ速度で再列挙 |
| 7 | `end()` → `begin()`を10回 | FSLSSUPPを毎回再設定して成功 |
| 8 | self-powered / bus-powered Hub | 電源要因と速度要因を分離 |
| 9 | 2～3種類の異なるHub controller | 特定Hub依存でないことを確認 |

最初の合否確認では項目1～3を優先する。class機能まで確認できれば、「列挙だけ成功してtransferが
失敗する」ケースも除外できる。

## 製品機能にするために必要な作業

### EspUsbHost側

1. 実機matrixの結果を保存し、対応するP4 revision、Arduino-ESP32、ESP-IDF、Hub VID/PIDを記録する。
2. 実験API名と契約を確定する。候補は `forceFullSpeed` または
   `maximumSpeed = USB_SPEED_FULL`。後者は将来拡張しやすい。
3. API reference、README、P4の性能説明に「HS connectorを使うがbusは12 Mbit/s」と明記する。
4. `begin()`、root-port power-on失敗、`end()`、再beginの回帰testを追加する。
5. HubなしのFS/LS device直結と、Hub配下の複数device/classを確認する。

### ESP-IDF側またはHAL連携

最大の未解決点はport recoveryである。ESP-IDFの `hcd_port_recover()` はDWC core soft resetを
行い、その後 `set_defaults()` を再適用するが、速度方針として `FSLSSUPP` を保持していない。
core resetでbitが初期値へ戻ると、その後の接続は再びHS negotiationを行う可能性がある。

恒久対応は次のいずれかが必要になる。

1. ESP-IDFのHCD/HAL configへmaximum host speedを追加し、初期化時とcore recovery後の両方で
   `FSLSSUPP` を再適用する（推奨）。
2. ESP-IDFにroot-port recovery hookを追加し、EspUsbHostがbitを再設定できるようにする。
3. private HCD構造へ依存してEspUsbHostから再設定する方法は、version互換性が低いため避ける。

upstreamへ提案するときは、Linux DWC2の `params.speed=FULL` と同様のHost maximum-speed設定として
説明できる。TT実装ではなく「root busをFSに制限する機能」であることを明確にする。

## 既知の制約と中止条件

- experimental modeではHS帯域を利用できない。
- DWC core error recovery後の速度固定維持は未確認・未対応。
- `HCFG` はESP-IDFの公開USB Host APIではなくLL/private寄りのAPIなので、IDF更新時にbuild確認が必要。
- Hubが依然 `speed=high` になる場合、root port停止順、register writeのreadback、P4 revision差を確認する。
- Hubはfull-speedになるが配下deviceが列挙されない場合、TTではなくHub driver、port reset、電源、
  channel数の問題として切り分ける。
- class transferでHost stackがabortする場合、ログを保存して通常のFS controllerで同じ構成と比較する。

実機で項目1～3が通るまでは通常APIへ昇格させず、`experimentalForceFullSpeed` のまま扱う。
