#include "EspUsbHost.h"

// ESP32-P4 HS-port FS-only hub probe.
//
// Connect a high-speed-capable USB 2.0 hub to the P4 HS port, then connect at
// least one FS or LS device behind it. The probe passes only when the hub itself
// enumerates at full speed and a downstream FS/LS device is visible. It sends no
// class-specific commands and does not modify device state.

EspUsbHost usb;

static constexpr uint32_t REPORT_INTERVAL_MS = 3000;
static bool passPrinted = false;

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "low";
  case USB_SPEED_FULL:
    return "full";
  case USB_SPEED_HIGH:
    return "high";
  default:
    return "unknown";
  }
}

static void reportTopology()
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  bool fullSpeedHubSeen = false;
  bool downstreamFsLsSeen = false;

  Serial.printf("TOPOLOGY devices=%u\n", static_cast<unsigned>(count));
  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostDeviceInfo &device = devices[i];
    Serial.printf("DEVICE address=%u parent=%u port=0x%02x speed=%s hub=%u protocol=0x%02x vid=%04x pid=%04x product=\"%s\"\n",
                  device.address,
                  device.parentAddress,
                  device.portId,
                  speedName(device.speed),
                  device.isHub ? 1 : 0,
                  device.deviceProtocol,
                  device.vid,
                  device.pid,
                  device.product);

    if (device.isHub)
    {
      if (device.speed == USB_SPEED_FULL)
      {
        fullSpeedHubSeen = true;
      }
      else if (device.speed == USB_SPEED_HIGH)
      {
        Serial.println("PROBE_FAIL reason=hub_enumerated_high_speed");
      }
    }
    else if (device.parentAddress != 0 &&
             (device.speed == USB_SPEED_FULL || device.speed == USB_SPEED_LOW))
    {
      downstreamFsLsSeen = true;
    }
  }

  if (!passPrinted && fullSpeedHubSeen && downstreamFsLsSeen)
  {
    passPrinted = true;
    Serial.println("PROBE_PASS hub_speed=full downstream_speed=fs_or_ls");
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("TEST_BEGIN p4_hs_fs_hub_probe");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("CONNECTED address=%u parent=%u speed=%s hub=%u protocol=0x%02x vid=%04x pid=%04x\n",
                                        device.address,
                                        device.parentAddress,
                                        speedName(device.speed),
                                        device.isHub ? 1 : 0,
                                        device.deviceProtocol,
                                        device.vid,
                                        device.pid); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           { Serial.printf("DISCONNECTED address=%u parent=%u hub=%u\n",
                                           device.address,
                                           device.parentAddress,
                                           device.isHub ? 1 : 0); });

  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  config.experimentalForceFullSpeed = true;
  if (!usb.begin(config))
  {
    Serial.printf("HOST_BEGIN_FAILED error=%s\n", usb.lastErrorName());
    Serial.println("PROBE_FAIL reason=host_begin");
    return;
  }

  Serial.println("HOST_READY port=hs bus_mode=full_speed_only");
  Serial.println("Attach a USB 2.0 hub and an FS/LS device behind it.");
}

void loop()
{
  static uint32_t lastReportMs = 0;
  if (millis() - lastReportMs >= REPORT_INTERVAL_MS)
  {
    lastReportMs = millis();
    reportTopology();
  }
  delay(10);
}
