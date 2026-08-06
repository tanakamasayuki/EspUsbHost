#include "USB.h"
#include "USBHID.h"

USBHID HID;

// A mouse in the shape issue #39 reports for a Logitech G502 HERO: 16 buttons,
// 16-bit X/Y, an 8-bit wheel and AC Pan, in an 8-byte report with no report ID.
// The Arduino Core USBHIDMouse used by the hid_mouse test can only produce the
// boot layout, which is the one layout that happens to work without reading the
// report descriptor.
static const uint8_t report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x02,       // Usage (Mouse)
    0xa1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0xa1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //     Usage Page (Button)
    0x19, 0x01,       //     Usage Minimum (1)
    0x29, 0x10,       //     Usage Maximum (16)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x10,       //     Report Count (16)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x16, 0x01, 0x80, //     Logical Minimum (-32767)
    0x26, 0xff, 0x7f, //     Logical Maximum (32767)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x02,       //     Report Count (2)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0x15, 0x81,       //     Logical Minimum (-127)
    0x25, 0x7f,       //     Logical Maximum (127)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0x09, 0x38,       //     Usage (Wheel)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0x05, 0x0c,       //     Usage Page (Consumer)
    0x0a, 0x38, 0x02, //     Usage (AC Pan)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x06,       //     Input (Data,Var,Rel)
    0xc0,             //   End Collection
    0xc0,             // End Collection
};

class ReportMouseDevice : public USBHIDDevice
{
public:
    ReportMouseDevice()
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

    bool send(uint16_t buttons, int16_t x, int16_t y, int8_t wheel, int8_t pan)
    {
        const uint8_t report[8] = {
            (uint8_t)(buttons & 0xff),
            (uint8_t)(buttons >> 8),
            (uint8_t)((uint16_t)x & 0xff),
            (uint8_t)((uint16_t)x >> 8),
            (uint8_t)((uint16_t)y & 0xff),
            (uint8_t)((uint16_t)y >> 8),
            (uint8_t)wheel,
            (uint8_t)pan,
        };
        return HID.SendReport(0, report, sizeof(report));
    }

    void click(uint16_t buttons)
    {
        send(buttons, 0, 0, 0, 0);
        delay(20);
        send(0, 0, 0, 0, 0);
    }
};

ReportMouseDevice Device;

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
        switch (command)
        {
        case 'r':
            Device.send(0, 300, 0, 0, 0);
            break;
        case 'l':
            Device.send(0, -300, 0, 0, 0);
            break;
        case 'd':
            Device.send(0, 0, 300, 0, 0);
            break;
        case 'u':
            Device.send(0, 0, -300, 0, 0);
            break;
        case 'w':
            Device.send(0, 0, 0, 1, 0);
            break;
        case 'p':
            Device.send(0, 0, 0, 0, -1);
            break;
        case 'm':
            Device.click(0x0001);
            break;
        case 'R':
            Device.click(0x0002);
            break;
        case 'x':
            Device.click(0x8000);
            break;
        }
    }
    delay(1);
}
