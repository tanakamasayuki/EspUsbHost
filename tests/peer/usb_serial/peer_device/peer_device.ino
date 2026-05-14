#include "USB.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;
#endif

void setup()
{
    Serial.begin(115200);
    USBSerial.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, [](void *, esp_event_base_t, int32_t, void *event_data) {
        arduino_usb_cdc_event_data_t *data = (arduino_usb_cdc_event_data_t *)event_data;
        Serial.printf("LINE_STATE dtr=%u rts=%u\n", data->line_state.dtr ? 1 : 0, data->line_state.rts ? 1 : 0);
    });
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
