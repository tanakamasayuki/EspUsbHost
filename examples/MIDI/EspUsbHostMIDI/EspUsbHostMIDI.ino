#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost MIDI example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    { Serial.printf("midi cable=%u cin=0x%02x status=0x%02x data1=%u data2=%u\n",
                                    message.cable,
                                    message.codeIndex,
                                    message.status,
                                    message.data1,
                                    message.data2); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'n')
    {
      usb.midiSendNoteOn(0, 60, 100);
    }
    else if (command == 'f')
    {
      usb.midiSendNoteOff(0, 60, 0);
    }
    else if (command == 'c')
    {
      usb.midiSendControlChange(0, 74, 64);
    }
    else if (command == 'p')
    {
      usb.midiSendProgramChange(0, 10);
    }
    else if (command == 'b')
    {
      usb.midiSendPitchBendSigned(0, 1024);
    }
    else if (command == 's')
    {
      const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
      usb.midiSendSysEx(sysex, sizeof(sysex));
    }
  }
  delay(1);
}
