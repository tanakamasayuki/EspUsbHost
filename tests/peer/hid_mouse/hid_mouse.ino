#include "EspUsbHost.h"

EspUsbHost usb;

static bool hostStarted = false;

static void startHost()
{
    if (hostStarted)
    {
        return; // idempotent: the test may start an already running host
    }
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
        return;
    }
    hostStarted = true;
    // Deliberately nothing is printed here. The test reads the enumeration
    // output that follows, and anything this sketch printed as an answer would
    // have to be consumed by an expect() in the fixture, which would advance the
    // reader past that output.
}

static void stopHost()
{
    usb.end();
    hostStarted = false;
    Serial.println("HOST_STATE idle devices=0");
}


void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    usb.onMouse([](const EspUsbHostMouseEvent &event)
                { Serial.printf("MOUSE x=%d y=%d wheel=%d buttons=%u previous=%u moved=%u changed=%u\n",
                                event.x,
                                event.y,
                                event.wheel,
                                event.buttons,
                                event.previousButtons,
                                event.moved ? 1 : 0,
                                event.buttonsChanged ? 1 : 0); });
    usb.addMouseListener([](const EspUsbHostMouseEvent &event)
                         { Serial.printf("MOUSE_LISTENER x=%d y=%d buttons=%u\n",
                                         event.x,
                                         event.y,
                                         event.buttons); });

    // The USB host is not started here. The peer board is flashed after this
    // sketch has booted and run setup(), so a host started here watches esptool
    // reset the peer and records the resulting enumerations as errors. The test
    // starts it once the peer is in place.
    Serial.println("HOST_STATE idle devices=0");
}

void loop()
{
    if (Serial.available() > 0)
    {
        const char command = Serial.read();
        if (command == 'Q')
        {
            // The device count is part of the answer so a test can wait for
            // enumeration by polling this, instead of waiting for a connect line
            // that is printed once and would be consumed by the wait itself.
            Serial.printf("HOST_STATE %s devices=%u\n",
                          hostStarted ? "running" : "idle",
                          static_cast<unsigned>(usb.deviceCount()));
        }
        else if (command == 'G')
        {
            startHost();
        }
        else if (command == 'H')
        {
            stopHost();
        }
    }
    delay(1);
}
