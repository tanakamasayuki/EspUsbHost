#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    { (void)message; });
  usb.begin();
}

void loop()
{
  usb.midiSendNoteOn(0, 60, 100);
  delay(1000);
}
