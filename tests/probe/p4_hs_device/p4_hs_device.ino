#include "USB.h"
#include "USBHIDKeyboard.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;
#endif

USBHIDKeyboard Keyboard;

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

  Serial.println("TEST_BEGIN p4_hs_device_probe");

  USB.onEvent([](void *, esp_event_base_t, int32_t eventId, void *)
              { Serial.printf("USB_DEVICE %s\n", usbEventName(static_cast<arduino_usb_event_t>(eventId))); });

  Keyboard.begin();
#if !ARDUINO_USB_CDC_ON_BOOT
  USBSerial.begin();
#endif

  if (!USB.begin())
  {
    Serial.println("USB_DEVICE_BEGIN_FAILED");
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  Serial.println("USB_DEVICE_READY hs");
  Serial.println("Connect the P4 HS device port to a PC or USB host.");
  Serial.println("Send 'k' on this serial monitor to emit HID keyboard 'a'.");
  Serial.println("Send 'c' to emit CDC text when USB CDC is enabled in this sketch.");
}

void loop()
{
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 5000)
  {
    lastPrint = millis();
    // Serial.println("USB_DEVICE_WAITING host_enumeration_must_be_checked_on_pc");
  }

  if (Serial.available() > 0)
  {
    const char c = Serial.read();
    if (c == 'k')
    {
      Keyboard.write('a');
      Serial.println("KEY_SENT a");
    }
    else if (c == 'c')
    {
#if !ARDUINO_USB_CDC_ON_BOOT
      USBSerial.println("p4 hs device cdc");
      Serial.println("CDC_SENT");
#else
      Serial.println("CDC_ON_BOOT_SKIP");
#endif
    }
  }

#if !ARDUINO_USB_CDC_ON_BOOT
  while (USBSerial.available() > 0)
  {
    Serial.print("CDC_RX ");
    while (USBSerial.available() > 0)
    {
      Serial.write(USBSerial.read());
    }
    Serial.println();
  }
#endif

  delay(1);
}
