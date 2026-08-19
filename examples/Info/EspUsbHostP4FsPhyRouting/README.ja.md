# EspUsbHostP4FsPhyRouting

> English: [README.md](README.md)

`EspUsbHost`を開始する前に、ESP32-P4のFull-speed OTG controllerをFSLS PHY0（GPIO24/GPIO25）へ接続するサンプルです。

## 使用する場面

USB Hostとして使いたいコネクタのD+/D-がGPIO24/GPIO25へ配線され、ボード側がHost用VBUS供給と、USB-Cの場合はrole/CC制御に対応しているESP32-P4ボードでのみ使用します。M5Stack Tab5はUSB-Cのデータ線がGPIO24/GPIO25へ接続されている例です。

P4のデフォルト割り当てでは、USB Serial/JTAGがGPIO24/GPIO25、USB OTG FSがGPIO26/GPIO27へ接続されています。このサンプルでは実行時に一対一で入れ替えます。どちらのUSB信号も複製されません。

| 機能 | 呼び出し前 | `usb_wrap_ll_phy_select(&USB_WRAP, 0)`の後 |
|------|------------|----------------------------------------------|
| USB OTG FS | GPIO26/GPIO27 | GPIO24/GPIO25 |
| USB Serial/JTAG | GPIO24/GPIO25 | GPIO26/GPIO27 |

eFuseは変更しません。チップをリセットすると、起動時のeFuse/デフォルト割り当てへ制御が戻ります。

## 重要事項

- 経路切り替えは`usb.begin()`より前に実行します。
- 他のOTG FS Host/Device driverよりも先に実行します。FS driverがすでに動作している場合は、停止・uninstallしてから経路を変更します。
- 割り当てを変更すると、GPIO24/GPIO25側のUSB Serial/JTAGは切断されます。それ以降のログを確認するには、外付けUSB-UARTまたは別のconsole接続が必要です。
- 内蔵USB Serial/JTAGが有効なら、入れ替え後にGPIO26/GPIO27側で再列挙する可能性があります。この呼び出しが2つ目のCDC stackを生成するわけではありません。
- 変更するのはUSB D+/D-の経路だけです。VBUS電源、過電流保護、USB-CのHost role制御は有効化しません。
- GPIO26/GPIO27側コネクタがHost用VBUSを出力していないこと、および移動後のUSB Serial/JTAGのDevice roleと競合する機器が接続されていないことを確認してください。
- USB Serial/JTAGを割り当てている間は、GPIO26/GPIO27を通常のGPIOや別peripheralとして使用できません。切り替え前にその使用を停止してください。後からUSB経路を元へ戻す場合は、`pinMode()`または対象peripheral driverの`begin()`を再実行し、pin mux、入出力方向、pull設定を再初期化してください。
- このサンプルを動かすために`USB_PHY_SEL` eFuseを書き込まないでください。eFuseの変更は不可逆です。
- ESP-IDFのP4用Low-level HAL APIを使用します。移植可能なライブラリAPIへ隠さず、意図的にアプリケーション側へ記述しています。

GPIO24/GPIO25へ接続された対象コネクタにUSBデバイスを接続してスケッチを実行してください。ライブラリは`ESP_USB_HOST_PORT_FULL_SPEED`を指定してFull-speed Host peripheralを開始します。

## 期待されるシリアル出力

```
Routing USB OTG FS to GPIO24/GPIO25 in 3 seconds.
USB Serial/JTAG on GPIO24/GPIO25 will disconnect.
```

この2行の表示後に割り当てが切り替わり、GPIO24/GPIO25側のUSB Serial/JTAGコンソールは切断されるため、以降はそこには何も表示されません。`usb.begin failed: ...`などのそれ以降のメッセージは、外付けUSB-UARTブリッジまたは別のconsole接続でのみ確認できます。
