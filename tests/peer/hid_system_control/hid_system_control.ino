#include "EspUsbHost.h"

EspUsbHost usb;

// The USB host is not started in setup(). The peer board is flashed after this
// sketch has booted, so a host started here observes esptool resetting the peer
// and records the resulting enumerations as errors. The test starts it once the
// peer is in place, and stops it again in the fixture teardown so the board is
// not left hosting USB after the run.
//
// Q / G / H rather than lower case: every lower-case letter is already a test
// command in one sketch or another, so the lifecycle commands get their own
// range instead of colliding per sketch.
static bool hostStarted = false;

static void startHost()
{
    if (hostStarted)
    {
        return; // idempotent
    }
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
        return;
    }
    hostStarted = true;
    // Nothing is printed here on purpose: an answer would have to be consumed by
    // an expect() in the fixture, which would advance the reader past the
    // enumeration output that follows and that the tests read.
}

static void stopHost()
{
    usb.end();
    hostStarted = false;
    Serial.println("HOST_STATE idle devices=0");
}

// Answered whenever it is asked, so a test can wait for enumeration by polling
// this rather than by waiting for a connect line that is printed once.
static bool handleLifecycle(char command)
{
    if (command == 'Q')
    {
        Serial.printf("HOST_STATE %s devices=%u\n",
                      hostStarted ? "running" : "idle",
                      static_cast<unsigned>(usb.deviceCount()));
        return true;
    }
    if (command == 'G')
    {
        startHost();
        return true;
    }
    if (command == 'H')
    {
        stopHost();
        return true;
    }
    return false;
}

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    // Intentionally use listeners without onSystemControl() to verify that a
    // listener by itself enables parsing and delivery.
    usb.addSystemControlListener([](const EspUsbHostSystemControlEvent &event)
                                 { Serial.printf("SYSTEM usage=0x%02x pressed=%u released=%u\n",
                                                 event.usage,
                                                 event.pressed ? 1 : 0,
                                                 event.released ? 1 : 0); });
    usb.addSystemControlListener([](const EspUsbHostSystemControlEvent &event)
                                 { Serial.printf("SYSTEM_LISTENER usage=0x%02x pressed=%u\n",
                                                 event.usage,
                                                 event.pressed ? 1 : 0); });

    Serial.println("HOST_STATE idle devices=0");
}

void loop()
{
    if (Serial.available() > 0)
    {
        handleLifecycle(Serial.read());
    }

    delay(1);
}
