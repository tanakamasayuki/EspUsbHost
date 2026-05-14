#include <Arduino.h>

#if ARDUINO_USB_MODE
void setup() {}
void loop() {}
#else

#include "USB.h"
#include "USBMIDI.h"

USBMIDI MIDI("EspUsbHost MIDI Peer");

void setup()
{
    Serial.begin(115200);
    MIDI.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'n')
        {
            MIDI.noteOn(64, 110, 0);
            Serial.println("DEVICE_TX_NOTE_ON");
        }
        else if (command == 'f')
        {
            MIDI.noteOff(64, 0, 0);
            Serial.println("DEVICE_TX_NOTE_OFF");
        }
        else if (command == 'p')
        {
            MIDI.programChange(10, 0);
            Serial.println("DEVICE_TX_PROGRAM");
        }
        else if (command == 'b')
        {
            MIDI.pitchBend(static_cast<uint16_t>(8192 + 1024), 0);
            Serial.println("DEVICE_TX_BEND");
        }
        else if (command == 'a')
        {
            MIDI.channelPressure(77, 0);
            Serial.println("DEVICE_TX_PRESSURE");
        }
        else if (command == 'y')
        {
            MIDI.polyPressure(60, 80, 0);
            Serial.println("DEVICE_TX_POLY_PRESSURE");
        }
        else if (command == 'c')
        {
            MIDI.controlChange(74, 64, 0);
            Serial.println("DEVICE_TX_CC");
        }
    }

    midiEventPacket_t packet = {0, 0, 0, 0};
    if (MIDI.readPacket(&packet))
    {
        Serial.printf("DEVICE_RX cin=%02x status=%02x data1=%u data2=%u\n",
                      packet.header & 0x0f,
                      packet.byte1,
                      packet.byte2,
                      packet.byte3);
    }
}

#endif
