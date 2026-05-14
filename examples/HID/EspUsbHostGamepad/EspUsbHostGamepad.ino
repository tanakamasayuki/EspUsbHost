#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr int8_t DEADZONE = 10;

static float normalize(int8_t val)
{
  if (val > -DEADZONE && val < DEADZONE)
    return 0.0f;
  return val / 127.0f;
}

static const char *hatName(uint8_t hat)
{
  switch (hat)
  {
  case 0:
    return "N";
  case 1:
    return "NE";
  case 2:
    return "E";
  case 3:
    return "SE";
  case 4:
    return "S";
  case 5:
    return "SW";
  case 6:
    return "W";
  case 7:
    return "NW";
  default:
    return "-";
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost gamepad example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
    float nx  = normalize(event.x);
    float ny  = normalize(event.y);
    float nz  = normalize(event.z);
    float nrz = normalize(event.rz);

    uint32_t pressed  = event.buttons & ~event.previousButtons;
    uint32_t released = event.previousButtons & ~event.buttons;

    Serial.printf("x=%6.3f y=%6.3f z=%6.3f rz=%6.3f hat=%s buttons=0x%08lx",
                  nx, ny, nz, nrz,
                  hatName(event.hat),
                  (unsigned long)event.buttons);
    if (pressed)  Serial.printf(" +0x%lx", (unsigned long)pressed);
    if (released) Serial.printf(" -0x%lx", (unsigned long)released);
    Serial.println(); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
