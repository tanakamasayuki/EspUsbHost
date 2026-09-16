#include "EspUsbHost.h"
#include "EspUsbDevice.h"

// Minimal reproduction of the ESP32-P4 role reversal fault, reduced to the
// lifecycle calls alone: no HID traffic, no payload checks.
//
// The board runs both roles on its two USB controllers at once and then swaps
// them, which is what a single-board loopback allows:
//
//   normal   Host = full-speed controller (rhport 0, internal FS PHY)
//            Device = high-speed controller (rhport 1, UTMI connector)
//   reverse  Host = high-speed controller, Device = full-speed controller
//
// EspUsbHost 2.9.0 aborts in the swap. end() cuts root port power before the
// client closes the attached device, so when the hub driver recycles the root
// port it finds it unpowered -- root_port_recycle() handles only ENABLED and
// RECOVERY and calls abort() on anything else. 2.8.0 never cut power and passes.
//
// Two conditions both matter, which is why the two-board probe/p4_end_teardown
// ladder does not reproduce it: the host is on the FULL-speed port when end()
// runs, and the device has already gone away because device.end() ran first.

EspUsbHost usb;
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static constexpr uint32_t ENUM_TIMEOUT_MS = 8000;

static volatile bool hostSawDevice = false;
static volatile bool hostGotKeys = false;
static volatile bool deviceGotLeds = false;

static bool waitFlag(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t deadline = millis() + timeoutMs;
  while (!flag && millis() < deadline)
  {
    delay(10);
  }
  return flag;
}

static bool waitForDevice()
{
  const uint32_t deadline = millis() + ENUM_TIMEOUT_MS;
  while (!hostSawDevice && millis() < deadline)
  {
    delay(10);
  }
  return hostSawDevice;
}

void setup()
{
  Serial.begin(115200);
  delay(2500);
  Serial.println("LOOPBACK_BEGIN p4_role_reversal");
  Serial.flush();

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &)
                        { hostSawDevice = true; });
  usb.onKeyboardState([](const auto &)
                      { hostGotKeys = true; });
  keyboard.onOutputReport([](const auto &)
                          { deviceGotLeds = true; });

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  Serial.printf("NORMAL_HOST_BEGIN ok=%u\n", usb.begin(hostConfig) ? 1 : 0);
  Serial.flush();

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4010;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbHost loopback role reversal";
  deviceConfig.serialNumber = "espusbhost-role-reversal";
  Serial.printf("NORMAL_DEVICE_BEGIN ok=%u\n", device.begin(deviceConfig) ? 1 : 0);
  Serial.flush();

  if (!waitForDevice())
  {
    Serial.println("PHASE_NORMAL fail");
    Serial.println("LOOPBACK_DONE p4_role_reversal");
    Serial.flush();
    return;
  }

  // Carry real traffic both ways first. Enumerating and tearing straight back
  // down does not reproduce the fault: the host has to have claimed the HID
  // interface and driven an OUT transfer to it.
  delay(500);
  keyboard.write("hello");
  Serial.printf("NORMAL_KEYS host_saw=%u\n", waitFlag(hostGotKeys, 5000) ? 1 : 0);
  Serial.flush();

  usb.setKeyboardLeds(true, false, false);
  delay(50);
  usb.setKeyboardLeds(false, true, false);
  delay(50);
  usb.setKeyboardLeds(false, false, true);
  delay(50);
  usb.setKeyboardLeds(false, false, false);
  Serial.printf("NORMAL_LEDS device_saw=%u\n", waitFlag(deviceGotLeds, 5000) ? 1 : 0);
  Serial.flush();

  Serial.println("PHASE_NORMAL ok");
  Serial.flush();

  // The swap. Everything above is setup.
  //
  // The two end() calls run back to back on purpose. Letting the host settle in
  // between -- even a Serial.println and a short delay -- is enough to make the
  // fault disappear, because the disconnect is then retired before the teardown
  // starts. The race between the device vanishing and end() cutting root port
  // power is the whole point.
  Serial.println("REVERSE_HOST_END_ENTER");
  Serial.flush();
  device.end();
  usb.end();
  Serial.println("REVERSE_HOST_END_OK");
  Serial.flush();
  delay(500);

  hostSawDevice = false;
  EspUsbHostConfig reverseHostConfig;
  reverseHostConfig.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  Serial.printf("REVERSE_HOST_BEGIN ok=%u\n", usb.begin(reverseHostConfig) ? 1 : 0);
  Serial.flush();

  deviceConfig.controller = EspUsbController::FullSpeed;
  Serial.printf("REVERSE_DEVICE_BEGIN ok=%u\n", device.begin(deviceConfig) ? 1 : 0);
  Serial.flush();

  // Say whether the restarted host can still see a device, not just that begin()
  // returned true: a root port left unpowered enumerates nothing.
  Serial.println(waitForDevice() ? "PHASE_REVERSE ok" : "PHASE_REVERSE fail");
  Serial.println("LOOPBACK_DONE p4_role_reversal");
  Serial.flush();
}

void loop()
{
  delay(1000);
}
