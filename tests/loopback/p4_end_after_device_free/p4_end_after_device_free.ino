#include "EspUsbHost.h"
#include "EspUsbDevice.h"
#include "usb/usb_host.h"

// usb.end() called the instant the host library has freed a device that went
// away, before the hub driver has run the port recovery that the free queued.
//
// Freeing the device (usbh handle_free: num_device-- and the DEV_FREE event)
// makes the hub driver queue PORT_REQ_RECOVER for the root port, and that
// request only runs in the library task's next usb_host_lib_handle_events().
// end() powering the port off in between moves the HCD port from RECOVERY to
// NOT_POWERED, the queued hcd_port_recover() then fails with
// ESP_ERR_INVALID_STATE, and hub.c root_port_req aborts on its ESP_ERROR_CHECK
// (espressif/usb 1.6.1). EspUsbHost 2.11.0 hit it in EspUsbDevice's loopback
// CCID test (device.end() and then host.end()) once in many runs.
//
// The window is a few microseconds of the library task, so a back-to-back
// device.end(); usb.end(); lands in it only by luck. This sketch aims at it: the
// host tasks run on core 0, this loop spins on core 1 reading
// usb_host_lib_info() and calls usb.end() as soon as the device count reaches 0.

EspUsbHost usb;
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static constexpr uint32_t ENUM_TIMEOUT_MS = 8000;
static constexpr unsigned CYCLES = 10;

static volatile bool hostSawDevice = false;

static bool waitForDevice()
{
  const uint32_t deadline = millis() + ENUM_TIMEOUT_MS;
  while (!hostSawDevice && millis() < deadline)
  {
    delay(10);
  }
  return hostSawDevice;
}

static int libDevices()
{
  usb_host_lib_info_t info = {};
  return usb_host_lib_info(&info) == ESP_OK ? info.num_devices : -1;
}

void setup()
{
  Serial.begin(115200);
  while (Serial.read() != 'S')
  {
    delay(10);
  }
  Serial.println("LOOPBACK_BEGIN p4_end_after_device_free");
  Serial.flush();

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &)
                        { hostSawDevice = true; });

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4011;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbHost loopback end after free";
  deviceConfig.serialNumber = "espusbhost-end-after-free";

  for (unsigned cycle = 1; cycle <= CYCLES; cycle++)
  {
    // Alternate the ports, as the CCID test does between its runs.
    const bool hostOnHighSpeed = (cycle % 2) == 0;
    hostSawDevice = false;
    EspUsbHostConfig hostConfig;
    hostConfig.port = hostOnHighSpeed ? ESP_USB_HOST_PORT_HIGH_SPEED : ESP_USB_HOST_PORT_FULL_SPEED;
    hostConfig.taskCore = 0;
    const bool hostOk = usb.begin(hostConfig);
    deviceConfig.controller = hostOnHighSpeed ? EspUsbController::FullSpeed : EspUsbController::HighSpeed;
    const bool deviceOk = device.begin(deviceConfig);
    const bool seen = hostOk && deviceOk && waitForDevice();
    Serial.printf("CYCLE_ENUM %u host=%s ok=%u\n", cycle, hostOnHighSpeed ? "hs" : "fs", seen ? 1 : 0);
    Serial.flush();
    if (!seen)
    {
      break;
    }
    delay(300);

    device.end();
    // Spin without yielding: delay() would let the library task run on and
    // close the window before end() starts.
    const uint32_t spinStartedAtMs = millis();
    int remaining = libDevices();
    while (remaining != 0 && millis() - spinStartedAtMs < 3000)
    {
      remaining = libDevices();
    }
    usb.end();
    Serial.printf("CYCLE_END %u freed=%u\n", cycle, remaining == 0 ? 1 : 0);
    Serial.flush();
    delay(200);
  }
  Serial.println("LOOPBACK_DONE p4_end_after_device_free");
  Serial.flush();
}

void loop()
{
  delay(1000);
}
