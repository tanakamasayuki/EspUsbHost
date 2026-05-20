#include "USB.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;
#endif

volatile bool lineCodingSeen = false;
volatile uint32_t lineCodingBaud = 0;
volatile uint8_t lineCodingStopBits = 0;
volatile uint8_t lineCodingParity = 0;
volatile uint8_t lineCodingDataBits = 0;

void onUsbCdcEvent(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
    (void)arg;
    (void)eventBase;
    if (eventId != ARDUINO_USB_CDC_LINE_CODING_EVENT || !eventData)
    {
        return;
    }
    arduino_usb_cdc_event_data_t *data = static_cast<arduino_usb_cdc_event_data_t *>(eventData);
    lineCodingBaud = data->line_coding.bit_rate;
    lineCodingStopBits = data->line_coding.stop_bits;
    lineCodingParity = data->line_coding.parity;
    lineCodingDataBits = data->line_coding.data_bits;
    lineCodingSeen = true;
}

void setup()
{
    Serial.begin(115200);
    USBSerial.enableReboot(false);
    USBSerial.onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, onUsbCdcEvent);
    USBSerial.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'd')
        {
            USBSerial.print("device to host");
        }
        else if (command == 'l')
        {
            Serial.printf("DEVICE_LINE_CODING seen=%u baud=%lu stop=%u parity=%u data=%u\n",
                          lineCodingSeen ? 1 : 0,
                          static_cast<unsigned long>(lineCodingBaud),
                          lineCodingStopBits,
                          lineCodingParity,
                          lineCodingDataBits);
        }
    }

    while (USBSerial.available() > 0)
    {
        Serial.print("DEVICE_RX ");
        while (USBSerial.available() > 0)
        {
            Serial.write(USBSerial.read());
        }
        Serial.println();
    }
    delay(1);
}
