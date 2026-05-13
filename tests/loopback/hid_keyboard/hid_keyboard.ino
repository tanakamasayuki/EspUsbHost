#include "EspUsbHost.h"
#include "USB.h"
#include "USBHIDKeyboard.h"

USBHIDKeyboard Keyboard;
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool keyReceived = false;

static const char *usbEventName(arduino_usb_event_t event)
{
  switch (event)
  {
  case ARDUINO_USB_STARTED_EVENT:
    return "started";
  case ARDUINO_USB_STOPPED_EVENT:
    return "stopped";
  case ARDUINO_USB_SUSPEND_EVENT:
    return "suspend";
  case ARDUINO_USB_RESUME_EVENT:
    return "resume";
  default:
    return "unknown";
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_hid_keyboard");

  USB.onEvent([](void *, esp_event_base_t, int32_t eventId, void *)
              { Serial.printf("USB_DEVICE %s\n", usbEventName(static_cast<arduino_usb_event_t>(eventId))); });

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", device.vid, device.pid);
                          deviceConnected = true;
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
                     if (event.ascii == 'a')
                     {
                       keyReceived = true;
                     }
                   }
                 });

  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(config))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  Keyboard.begin();
  if (!USB.begin())
  {
    Serial.println("USB_DEVICE_BEGIN_FAILED");
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("USB_DEVICE_READY");

  const uint32_t connectStart = millis();
  while (!deviceConnected && millis() - connectStart < 30000)
  {
    delay(10);
  }

  if (!deviceConnected)
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);
  Keyboard.write('a');

  const uint32_t keyStart = millis();
  while (!keyReceived && millis() - keyStart < 5000)
  {
    delay(10);
  }

  Serial.println(keyReceived ? "TEST_END ok" : "TEST_END fail");
  Serial.println(keyReceived ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
