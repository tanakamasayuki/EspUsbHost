#include "EspUsbHost.h"

EspUsbHost usb;

static const char *const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"};

static void printNote(uint8_t note)
{
  Serial.printf("%s%d", NOTE_NAMES[note % 12], (int)(note / 12) - 1);
}

static void printMidiMessage(const EspUsbHostMidiMessage &msg)
{
  uint8_t type = msg.status & 0xF0;
  uint8_t channel = (msg.status & 0x0F) + 1;

  switch (type)
  {
  case 0x80:
    Serial.printf("Note Off   ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" vel=%u\n", msg.data2);
    break;
  case 0x90:
    Serial.printf("Note On    ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" vel=%u\n", msg.data2);
    break;
  case 0xA0:
    Serial.printf("Aftertouch ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" pressure=%u\n", msg.data2);
    break;
  case 0xB0:
    Serial.printf("CC         ch=%u ctrl=%u val=%u\n", channel, msg.data1, msg.data2);
    break;
  case 0xC0:
    Serial.printf("Program    ch=%u prog=%u\n", channel, msg.data1);
    break;
  case 0xD0:
    Serial.printf("Pressure   ch=%u val=%u\n", channel, msg.data1);
    break;
  case 0xE0:
    Serial.printf("PitchBend  ch=%u val=%d\n", channel,
                  (int)(((uint16_t)msg.data2 << 7) | msg.data1) - 8192);
    break;
  default:
    Serial.printf("System     status=0x%02x data1=%u data2=%u\n",
                  msg.status, msg.data1, msg.data2);
    break;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("n:NoteOn C4  f:NoteOff C4  c:CC#74  p:Program 10  b:PitchBend  s:SysEx");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    { printMidiMessage(message); });

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
      usb.midiSendNoteOn(0, 60, 100);
    else if (command == 'f')
      usb.midiSendNoteOff(0, 60, 0);
    else if (command == 'c')
      usb.midiSendControlChange(0, 74, 64);
    else if (command == 'p')
      usb.midiSendProgramChange(0, 10);
    else if (command == 'b')
      usb.midiSendPitchBendSigned(0, 1024);
    else if (command == 's')
    {
      const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
      usb.midiSendSysEx(sysex, sizeof(sysex));
    }
  }
  delay(1);
}
