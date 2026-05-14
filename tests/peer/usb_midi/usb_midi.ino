#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                      { Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
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
            Serial.printf("MIDI_TX_NOTE_ON %u\n", usb.midiSendNoteOn(0, 60, 100) ? 1 : 0);
        }
        else if (command == 'f')
        {
            Serial.printf("MIDI_TX_NOTE_OFF %u\n", usb.midiSendNoteOff(0, 60, 0) ? 1 : 0);
        }
        else if (command == 'c')
        {
            Serial.printf("MIDI_TX_CC %u\n", usb.midiSendControlChange(0, 74, 64) ? 1 : 0);
        }
        else if (command == 'p')
        {
            Serial.printf("MIDI_TX_PROGRAM %u\n", usb.midiSendProgramChange(0, 10) ? 1 : 0);
        }
        else if (command == 'b')
        {
            Serial.printf("MIDI_TX_BEND %u\n", usb.midiSendPitchBend(0, 8192 + 1024) ? 1 : 0);
        }
        else if (command == 'a')
        {
            Serial.printf("MIDI_TX_PRESSURE %u\n", usb.midiSendChannelPressure(0, 77) ? 1 : 0);
        }
        else if (command == 'y')
        {
            Serial.printf("MIDI_TX_POLY_PRESSURE %u\n", usb.midiSendPolyPressure(0, 60, 80) ? 1 : 0);
        }
        else if (command == 's')
        {
            const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
            Serial.printf("MIDI_TX_SYSEX %u\n", usb.midiSendSysEx(sysex, sizeof(sysex)) ? 1 : 0);
        }
    }
    delay(1);
}
