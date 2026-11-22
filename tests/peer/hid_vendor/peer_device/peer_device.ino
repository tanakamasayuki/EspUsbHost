#include "USB.h"
#include "USBHIDVendor.h"

USBHIDVendor Vendor;

// Each report is printed once, from the event callback, as one line built
// before it is written. The same report also reaches the Vendor stream; printing
// it from loop() as well put two writers on Serial at once, and their lines
// interleaved ("DEVICE_OUTPUT hostDEVICE_OUTPUT  output") often enough to fail
// the test.
static void vendorEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != ARDUINO_USB_HID_VENDOR_EVENTS)
    {
        return;
    }

    arduino_usb_hid_vendor_event_data_t *data = static_cast<arduino_usb_hid_vendor_event_data_t *>(event_data);
    String line;
    if (event_id == ARDUINO_USB_HID_VENDOR_OUTPUT_EVENT)
    {
        line = "DEVICE_OUTPUT ";
    }
    else if (event_id == ARDUINO_USB_HID_VENDOR_SET_FEATURE_EVENT)
    {
        line = "DEVICE_FEATURE ";
    }
    else
    {
        return;
    }

    for (uint16_t i = 0; i < data->len && data->buffer[i] != 0; i++)
    {
        line += static_cast<char>(data->buffer[i]);
    }
    line += "\r\n";
    Serial.print(line);
}

void setup()
{
    Serial.begin(115200);
    Vendor.onEvent(vendorEventCallback);
    Vendor.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'h')
        {
            Vendor.print("hello vendor");
        }
    }
    // The callback above already printed the report; drain the stream copy so
    // its buffer does not fill.
    while (Vendor.available() > 0)
    {
        Vendor.read();
    }
    delay(1);
}
