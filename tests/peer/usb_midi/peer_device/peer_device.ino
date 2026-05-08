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
