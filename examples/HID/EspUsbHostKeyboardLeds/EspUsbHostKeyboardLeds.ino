#include "EspUsbHost.h"

EspUsbHost usb;

bool numLock = false;
bool capsLock = false;
bool scrollLock = false;

static void applyLeds()
{
  bool ok = usb.setKeyboardLeds(numLock, capsLock, scrollLock);
  Serial.printf("leds: num=%u caps=%u scroll=%u %s\n",
                numLock,
                capsLock,
                scrollLock,
                ok ? "OK" : "NG");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("Keyboard LED control");
  Serial.println("n: NumLock, c: CapsLock, s: ScrollLock, 0: all off");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n",
                                        device.vid,
                                        device.pid,
                                        device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  while (Serial.available())
  {
    char command = Serial.read();
    if (command == 'n')
    {
      numLock = !numLock;
      applyLeds();
    }
    else if (command == 'c')
    {
      capsLock = !capsLock;
      applyLeds();
    }
    else if (command == 's')
    {
      scrollLock = !scrollLock;
      applyLeds();
    }
    else if (command == '0')
    {
      numLock = false;
      capsLock = false;
      scrollLock = false;
      applyLeds();
    }
  }
}
