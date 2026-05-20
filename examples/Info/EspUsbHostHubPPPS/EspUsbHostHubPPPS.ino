#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static constexpr uint8_t DEFAULT_HUB_ADDRESS = 1;

static uint8_t targetHubAddress = DEFAULT_HUB_ADDRESS;
static uint8_t selectedPort = 1;
static bool printedInitialHub = false;
static uint32_t lastDeviceEventMs = 0;

static bool isPowered(uint16_t status)
{
  return (status & 0x0100) != 0;
}

static bool isConnected(uint16_t status)
{
  return (status & 0x0001) != 0;
}

static void printPortStatus(uint8_t hubAddress, uint8_t port)
{
  // en: Hub port status contains both current state bits and latched change bits.
  // ja: Hubポート状態には、現在状態のビットとラッチされた変化ビットの両方が含まれます。
  uint16_t status = 0;
  uint16_t change = 0;
  if (!usb.getHubPortStatus(hubAddress, port, status, change))
  {
    Serial.printf("  port %u: status unavailable\n", port);
    return;
  }

  Serial.printf("  port %u: status=0x%04x change=0x%04x powered=%u connected=%u enabled=%u\n",
                port,
                status,
                change,
                isPowered(status) ? 1 : 0,
                isConnected(status) ? 1 : 0,
                (status & 0x0002) ? 1 : 0);
}

static bool printHub(uint8_t hubAddress)
{
  EspUsbHostHubInfo hub;
  if (!usb.getHubInfo(hubAddress, hub))
  {
    return false;
  }

  targetHubAddress = hubAddress;
  Serial.printf("hub address=%u ports=%u ppps=%u ganged_power=%u no_power_switch=%u\n",
                hub.address,
                hub.portCount,
                hub.perPortPowerSwitching ? 1 : 0,
                hub.gangedPowerSwitching ? 1 : 0,
                hub.noPowerSwitching ? 1 : 0);

  for (uint8_t port = 1; port <= hub.portCount; port++)
  {
    printPortStatus(hub.address, port);
  }

  if (!hub.perPortPowerSwitching)
  {
    Serial.println("This hub does not report PPPS support. Port power commands may affect all ports or fail.");
  }
  return true;
}

static bool printFirstAvailableHub()
{
  // en: Try the root hub address first, then any tracked downstream hub devices.
  // ja: まずルートHubアドレスを試し、その後に追跡中の下流Hubデバイスを調べます。
  if (printHub(DEFAULT_HUB_ADDRESS))
  {
    return true;
  }

  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  for (size_t i = 0; i < count; i++)
  {
    if (devices[i].isHub && printHub(devices[i].address))
    {
      return true;
    }
  }

  Serial.println("No USB hub was found.");
  return false;
}

static void setSelectedPortPower(bool enable)
{
  // en: PPPS is hub-dependent; refresh the selected port after sending the power request.
  // ja: PPPSはHub依存なので、電源要求の送信後に選択ポートの状態を再取得します。
  Serial.printf("hub=%u port=%u power=%s\n",
                targetHubAddress,
                selectedPort,
                enable ? "on" : "off");
  const bool ok = usb.setHubPortPower(targetHubAddress, selectedPort, enable);
  Serial.printf("request: %s\n", ok ? "OK" : "FAIL");
  delay(500);
  printPortStatus(targetHubAddress, selectedPort);
}

static void printHelp()
{
  Serial.println("Commands:");
  Serial.println("  r  refresh hub and port status");
  Serial.println("  1-9  select port");
  Serial.println("  o  power off selected port");
  Serial.println("  n  power on selected port");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHostHubPPPS start");
  Serial.println("Connect a USB hub that supports per-port power switching.");
  printHelp();

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          lastDeviceEventMs = millis();
                          Serial.printf("CONNECTED address=%u portId=0x%02x hub=%u vid=%04x pid=%04x\n",
                                        device.address,
                                        device.portId,
                                        device.isHub ? 1 : 0,
                                        device.vid,
                                        device.pid); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             lastDeviceEventMs = millis();
                             Serial.printf("DISCONNECTED address=%u portId=0x%02x vid=%04x pid=%04x\n",
                                           device.address,
                                           device.portId,
                                           device.vid,
                                           device.pid); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
}

void loop()
{
  // en: Wait briefly after hotplug so hub descriptors and port status are ready.
  // ja: ホットプラグ直後はHubディスクリプタとポート状態が揃うまで少し待ちます。
  if (!printedInitialHub && millis() - lastDeviceEventMs > 2000)
  {
    printedInitialHub = true;
    printFirstAvailableHub();
  }

  if (!Serial.available())
  {
    delay(10);
    return;
  }

  const char command = Serial.read();
  // en: Single-key commands keep the example usable from Arduino Serial Monitor.
  // ja: Arduinoシリアルモニターから扱いやすいよう、1文字コマンドにしています。
  if (command >= '1' && command <= '9')
  {
    selectedPort = command - '0';
    Serial.printf("selected port=%u\n", selectedPort);
    printPortStatus(targetHubAddress, selectedPort);
  }
  else if (command == 'r')
  {
    printFirstAvailableHub();
  }
  else if (command == 'o')
  {
    setSelectedPortPower(false);
  }
  else if (command == 'n')
  {
    setSelectedPortPower(true);
  }
}
