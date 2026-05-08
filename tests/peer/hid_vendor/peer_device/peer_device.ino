#include "USB.h"
#include "USBHIDVendor.h"

USBHIDVendor Vendor;
static bool outputLineOpen = false;

static void vendorEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != ARDUINO_USB_HID_VENDOR_EVENTS)
    {
        return;
    }

    arduino_usb_hid_vendor_event_data_t *data = static_cast<arduino_usb_hid_vendor_event_data_t *>(event_data);
    if (event_id == ARDUINO_USB_HID_VENDOR_OUTPUT_EVENT)
    {
        Serial.print("DEVICE_OUTPUT ");
    }
    else if (event_id == ARDUINO_USB_HID_VENDOR_SET_FEATURE_EVENT)
    {
        Serial.print("DEVICE_FEATURE ");
    }
    else
    {
        return;
    }

    for (uint16_t i = 0; i < data->len && data->buffer[i] != 0; i++)
    {
        Serial.write(data->buffer[i]);
    }
    Serial.println();
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
    while (Vendor.available() > 0)
    {
        if (!outputLineOpen)
        {
            Serial.print("DEVICE_OUTPUT ");
            outputLineOpen = true;
        }
        int value = Vendor.read();
        if (value > 0)
        {
            Serial.write(static_cast<uint8_t>(value));
        }
    }
    if (outputLineOpen)
    {
        Serial.println();
        outputLineOpen = false;
    }
    delay(1);
}
