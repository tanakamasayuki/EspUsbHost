#include "EspUsbHost.h"

EspUsbHost usb;

static constexpr int8_t DEADZONE = 10;

static float normalize(int8_t val)
{
  // en: Apply a small deadzone before converting the signed 8-bit axis to -1.0..1.0.
  // ja: 符号付き8bit軸を-1.0〜1.0へ変換する前に、小さなデッドゾーンを適用します。
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

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
    // en: Print both analog state and button edge changes for each parsed report.
    // ja: パース済みレポートごとに、アナログ状態とボタン変化の両方を表示します。
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
