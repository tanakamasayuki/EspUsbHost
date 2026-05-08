#include "USB.h"
#include "USBHID.h"

USBHID HID;

static const uint8_t report_descriptor[] = {
    0x05,
    0x01,
    0x09,
    0x04,
    0xa1,
    0x01,
    0xa1,
    0x00,
    0x05,
    0x01,
    0x09,
    0x30,
    0x09,
    0x31,
    0x09,
    0x32,
    0x09,
    0x33,
    0x09,
    0x34,
    0x09,
    0x35,
    0x09,
    0x36,
    0x09,
    0x36,
    0x15,
    0x81,
    0x25,
    0x7f,
    0x75,
    0x08,
    0x95,
    0x08,
    0x81,
    0x02,
    0xc0,
    0xc0,
};

class CustomHIDDevice : public USBHIDDevice
{
public:
    CustomHIDDevice()
    {
        static bool initialized = false;
        if (!initialized)
        {
            initialized = true;
            HID.addDevice(this, sizeof(report_descriptor));
        }
    }

    void begin()
    {
        HID.begin();
    }

    uint16_t _onGetDescriptor(uint8_t *buffer)
    {
        memcpy(buffer, report_descriptor, sizeof(report_descriptor));
        return sizeof(report_descriptor);
    }

    bool send(const uint8_t *value)
    {
        return HID.SendReport(0, value, 8);
    }
};

CustomHIDDevice Device;

void setup()
{
    Serial.begin(115200);
    Device.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'a')
        {
            const uint8_t axis[8] = {0x01, 0x7f, 0x81, 0x22, 0x33, 0x44, 0x55, 0x66};
            Device.send(axis);
        }
    }
    delay(1);
}
