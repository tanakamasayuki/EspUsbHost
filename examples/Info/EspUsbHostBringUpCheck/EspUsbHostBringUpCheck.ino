// en: First tool to run on a new board or with a new device: does USB Host come
//     up at all, and does the device enumerate?
// ja: 新しいボードや新しいデバイスで最初に動かすツールです。USB Hostが起動するか、
//     デバイスが列挙されるかだけを確認します。
//
// en: It answers three questions, in order: did begin() succeed, did anything
//     enumerate, and at what speed. When nothing enumerates it prints the
//     checklist of physical causes instead of leaving a silent log.
// ja: 「begin()が成功したか」「何か列挙されたか」「どの速度か」を順に確認します。
//     何も列挙されない場合は、無言のログではなく物理的な原因のチェックリストを表示します。

#include "EspUsbHost.h"
#include "espusbhost_version.h"

EspUsbHost usb;

// en: ESP32-P4 only. Set to 1 to use the high-speed OTG port instead of the
//     full-speed one. Other targets ignore the port setting entirely.
// ja: ESP32-P4専用。1にするとフルスピードではなくハイスピードOTGポートを使います。
//     他のターゲットではポート設定は無視されます。
#define USE_HIGH_SPEED_PORT 0

static constexpr uint32_t REPORT_INTERVAL_MS = 2000;
static constexpr uint32_t NO_DEVICE_HINT_MS = 10000;

static uint32_t startedAt = 0;
static uint32_t lastReportMs = 0;
static bool hintPrinted = false;
static bool beginOk = false;

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "low-speed (1.5Mbps)";
  case USB_SPEED_FULL:
    return "full-speed (12Mbps)";
  case USB_SPEED_HIGH:
    return "high-speed (480Mbps)";
  default:
    return "unknown";
  }
}

static void printEnvironment()
{
  Serial.println();
  Serial.println("--- environment ---");
  Serial.printf("chip           : %s rev %u\n", ESP.getChipModel(), (unsigned)ESP.getChipRevision());
  Serial.printf("arduino-esp32  : %s\n", ESP_ARDUINO_VERSION_STR);
  Serial.printf("EspUsbHost     : %s\n", ESPUSBHOST_VERSION_STR);
  Serial.printf("free heap      : %u bytes\n", (unsigned)ESP.getFreeHeap());
#if CONFIG_IDF_TARGET_ESP32P4
  Serial.printf("host port      : %s\n", USE_HIGH_SPEED_PORT ? "high-speed OTG" : "full-speed OTG");
#else
  Serial.println("host port      : full-speed OTG (this target has no high-speed host)");
#endif
  Serial.printf("channel budget : %u endpoint channels\n", (unsigned)usb.maxEndpointChannelCount());
  Serial.println();
}

// en: Everything here is outside the ESP32: no amount of firmware fixes it.
// ja: ここに挙げた原因はすべてESP32の外側にあり、ファームウェアでは直せません。
static void printNoDeviceHints()
{
  Serial.println();
  Serial.println("--- no device enumerated: check these, in this order ---");
  Serial.println("1. VBUS: does this connector supply 5V to the device?");
  Serial.println("   Many dev boards (e.g. ESP32-S3-DevKitC-1) do NOT. Measure it,");
  Serial.println("   or use a self-powered USB hub between the board and the device.");
  Serial.println("2. Connector: is the cable in the USB-OTG port, not the UART/console port?");
  Serial.println("   Connector position differs per board; check the schematic, not the label.");
  Serial.println("3. Cable: charge-only cables have no D+/D-. Try a known-good data cable.");
  Serial.println("4. Power draw: a bus-powered hub, a 2.5-inch HDD, or a printer may need");
  Serial.println("   more current than the board can source. Use a self-powered hub.");
  Serial.println("5. Device: try a plain USB keyboard or flash drive first. If that");
  Serial.println("   enumerates, the board is fine and the problem is device-specific.");
  Serial.println("6. Log level: rebuild with Core Debug Level = Verbose to see the");
  Serial.println("   ESP-IDF enumeration errors (descriptor too long, no more channels, ...).");
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  Serial.println("EspUsbHost bring-up check start");

  printEnvironment();

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
    Serial.println();
    Serial.printf("ENUMERATED address=%u speed=%s\n", device.address, speedName(device.speed));
    Serial.printf("  %04x:%04x \"%s\" / \"%s\"\n", device.vid, device.pid, device.manufacturer, device.product);
    Serial.printf("  device class=0x%02x subclass=0x%02x protocol=0x%02x interfaces=%u\n",
                  device.deviceClass, device.deviceSubClass, device.deviceProtocol,
                  device.configurationInterfaceCount);
    Serial.printf("  library support=%s hub=%s max_power=%umA %s\n",
                  device.supported ? "yes" : "no",
                  device.isHub ? "yes" : "no",
                  (unsigned)device.configurationMaxPower * 2,
                  (device.configurationAttributes & 0x40) ? "(self-powered)" : "(bus-powered)");
    Serial.printf("  channels claimed=%u/%u\n",
                  (unsigned)usb.endpointChannelCount(),
                  (unsigned)usb.maxEndpointChannelCount());
    Serial.println("  Next: run examples/Info/EspUsbHostDeviceExplorer for the full layout."); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           { Serial.printf("REMOVED address=%u %04x:%04x\n", device.address, device.vid, device.pid); });

  EspUsbHostConfig config;
#if CONFIG_IDF_TARGET_ESP32P4
  config.port = USE_HIGH_SPEED_PORT ? ESP_USB_HOST_PORT_HIGH_SPEED : ESP_USB_HOST_PORT_FULL_SPEED;
#endif

  beginOk = usb.begin(config);
  if (beginOk)
  {
    Serial.println("usb.begin(): ok -- the host controller is running");
    Serial.println("Plug in a USB device now.");
  }
  else
  {
    // en: A failure here is a software/build problem, not a cabling problem.
    // ja: ここでの失敗は配線ではなく、ソフト/ビルド側の問題です。
    Serial.printf("usb.begin(): FAILED: %s\n", usb.lastErrorName());
    Serial.println("This is a build or configuration problem, not a wiring problem:");
    Serial.println("- Is the target actually an ESP32-S2 / S3 / P4?");
    Serial.println("- Is the arduino-esp32 core new enough (S2/S3: 3.2.0+, P4: 3.3.1+)?");
    Serial.println("- On ESP32-P4, is another driver already using this OTG port?");
  }

  startedAt = millis();
  lastReportMs = startedAt;
}

void loop()
{
  const uint32_t now = millis();
  if (beginOk && now - lastReportMs >= REPORT_INTERVAL_MS)
  {
    lastReportMs = now;
    const size_t count = usb.deviceCount();
    Serial.printf("[%5lus] devices=%u channels=%u/%u heap=%u\n",
                  (unsigned long)((now - startedAt) / 1000),
                  (unsigned)count,
                  (unsigned)usb.endpointChannelCount(),
                  (unsigned)usb.maxEndpointChannelCount(),
                  (unsigned)ESP.getFreeHeap());

    if (count == 0 && !hintPrinted && now - startedAt >= NO_DEVICE_HINT_MS)
    {
      hintPrinted = true;
      printNoDeviceHints();
    }
    if (count > 0)
    {
      hintPrinted = false;
    }
  }
  delay(10);
}
