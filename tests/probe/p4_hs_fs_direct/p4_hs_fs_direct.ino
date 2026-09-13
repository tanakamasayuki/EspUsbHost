#include "EspUsbHost.h"

#include <esp_timer.h>

// ESP32-P4 HS-port forced-full-speed probe, without a hub.
//
// docs/p4-hs-port-fs-only-hub.ja.md proposes driving the P4 high-speed physical
// port as a full-speed-only host by setting HCFG.FSLSSUPP, so that a USB 2.0 hub
// enumerates as a full-speed hub and the FS/LS devices behind it need no
// Transaction Translator. That was written without a hardware result.
//
// This probe tests the foundation of that claim in isolation: with one device
// wired directly to the HS port and no hub anywhere, does the bit actually make
// the root port come up at full speed, and does a device still enumerate on it?
// It runs the same link twice -- forced full speed, then the default -- so the
// two speeds are measured on one cable with one device, and reports the bulk IN
// rate of each as a cross-check that the reported speed is the speed the bus is
// really running at.
//
// Peer: a second board flashed with tests/peer/usb_vendor_read/peer_device
// (profile p4_peer_device), whose OTG HS port is wired to this board's.

EspUsbHost usb;

static constexpr uint16_t PEER_VID = 0x303a;
static constexpr uint16_t PEER_PID = 0x4019;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
static constexpr size_t STREAM_BYTES = 64 * 1024;
static constexpr uint32_t STREAM_TIMEOUT_MS = 20000;
static constexpr size_t READ_TRANSFER_BYTES = 8192;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static volatile size_t streamBytes = 0;
static volatile uint32_t streamBad = 0;
static volatile bool streamArmed = false;

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

static usb_speed_t peerSpeed()
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  for (size_t i = 0; i < count; i++)
  {
    Serial.printf("DEVICE address=%u parent=%u speed=%s vid=%04x pid=%04x product=\"%s\"\n",
                  devices[i].address,
                  devices[i].parentAddress,
                  speedName(devices[i].speed),
                  devices[i].vid,
                  devices[i].pid,
                  devices[i].product);
    if (devices[i].vid == PEER_VID && devices[i].pid == PEER_PID)
    {
      return devices[i].speed;
    }
  }
  return USB_SPEED_LOW; // not found; the caller reports it as a failure
}

// Ask the peer for STREAM_BYTES and time them. Returns MB/s (decimal), or 0.
static double measure()
{
  streamArmed = false;
  delay(150);
  streamBytes = 0;
  streamBad = 0;
  streamArmed = true;

  const size_t bytes = STREAM_BYTES;
  const uint8_t request[5] = {'S',
                              static_cast<uint8_t>(bytes & 0xff),
                              static_cast<uint8_t>((bytes >> 8) & 0xff),
                              static_cast<uint8_t>((bytes >> 16) & 0xff),
                              static_cast<uint8_t>((bytes >> 24) & 0xff)};
  const int64_t startedAt = esp_timer_get_time();
  if (!usb.vendorWrite(request, sizeof(request), deviceAddress))
  {
    streamArmed = false;
    return 0.0;
  }
  const uint32_t deadline = millis() + STREAM_TIMEOUT_MS;
  while (streamBytes < bytes && millis() < deadline)
  {
    delay(1);
  }
  const int64_t finishedAt = esp_timer_get_time();
  streamArmed = false;
  if (streamBytes < bytes)
  {
    return 0.0;
  }
  const double seconds = static_cast<double>(finishedAt - startedAt) / 1000000.0;
  return seconds > 0.0 ? (static_cast<double>(streamBytes) / seconds) / 1000000.0 : 0.0;
}

// One begin()/end() cycle at the requested bus mode.
static usb_speed_t runCondition(const char *mode, bool forceFullSpeed)
{
  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  config.experimentalForceFullSpeed = forceFullSpeed;

  connected = false;
  deviceAddress = 0;

  if (!usb.begin(config))
  {
    Serial.printf("PROBE_FAIL reason=begin_failed mode=%s error=%s\n", mode, usb.lastErrorName());
    return USB_SPEED_LOW;
  }
  Serial.printf("HOST_READY port=hs bus_mode=%s\n", forceFullSpeed ? "full_speed_only" : "default");

  const uint32_t deadline = millis() + CONNECT_TIMEOUT_MS;
  while (!connected && millis() < deadline)
  {
    delay(10);
  }
  if (!connected)
  {
    Serial.printf("RESULT mode=%s speed=none mbps=0.000 bad=0\n", mode);
    usb.end();
    return USB_SPEED_LOW;
  }

  const usb_speed_t speed = peerSpeed();

  const bool opened = usb.vendorOpen(deviceAddress, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS,
                                     READ_TRANSFER_BYTES);
  Serial.printf("ENUM mode=%s speed=%s in_mps=%u out_mps=%u xfer=%u\n",
                mode,
                speedName(speed),
                usb.vendorInPacketSize(deviceAddress),
                usb.vendorOutPacketSize(deviceAddress),
                static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)));
  Serial.flush();

  double mbps = 0.0;
  if (opened && usb.vendorReadQueueBegin(2, READ_TRANSFER_BYTES, deviceAddress))
  {
    mbps = measure();
  }

  Serial.printf("RESULT mode=%s speed=%s in_mps=%u out_mps=%u xfer=%u mbps=%.3f bad=%lu\n",
                mode,
                speedName(speed),
                usb.vendorInPacketSize(deviceAddress),
                usb.vendorOutPacketSize(deviceAddress),
                static_cast<unsigned>(usb.vendorInTransferBytes(deviceAddress)),
                mbps,
                static_cast<unsigned long>(streamBad));
  usb.vendorReadQueueEnd(deviceAddress);
  usb.end();
  delay(500); // let the peer see the disconnect before the next begin()
  return speed;
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  Serial.println("TEST_BEGIN p4_hs_fs_direct_probe");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          if (device.vid == PEER_VID && device.pid == PEER_PID)
                          {
                            deviceAddress = device.address;
                            connected = true;
                          }
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             if (device.address == deviceAddress)
                             {
                               connected = false;
                             }
                           });

  // Phase check only: the ramp is continuous, so the first byte of each chunk
  // must be (bytes so far) & 0xff. Cheap enough not to disturb the measurement.
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     if (!streamArmed || data.length == 0)
                     {
                       return;
                     }
                     if (data.data[0] != static_cast<uint8_t>(streamBytes & 0xff))
                     {
                       streamBad++;
                     }
                     streamBytes += data.length;
                   });

  // Default first on purpose. The forced mode was measured before and ended in a
  // panic inside the host library's teardown, so running the ordinary mode first
  // says whether that teardown is broken generally or only after the forced one.
  const usb_speed_t normal = runCondition("default", false);
  const usb_speed_t forced = runCondition("fs_only", true);

  if (forced == USB_SPEED_FULL && normal == USB_SPEED_HIGH)
  {
    Serial.println("PROBE_PASS forced=full default=high");
  }
  else if (forced == USB_SPEED_HIGH)
  {
    // The bit did not take effect before reset negotiation.
    Serial.println("PROBE_FAIL reason=forced_mode_enumerated_high_speed");
  }
  else if (forced == USB_SPEED_FULL)
  {
    // Forcing worked but the baseline did not, so the comparison is incomplete.
    Serial.println("PROBE_FAIL reason=default_mode_did_not_reach_high_speed");
  }
  else
  {
    Serial.println("PROBE_FAIL reason=no_device_in_forced_mode");
  }
}

void loop()
{
  delay(100);
}
