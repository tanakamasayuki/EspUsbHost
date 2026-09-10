#include "EspUsbHost.h"

// Host side of the NKRO peer test. Counts how many keys the attached keyboard
// holds at the same time and reports the maximum, which is what distinguishes
// NKRO (bitmap, unlimited) from a boot keyboard (6-key limit).

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

static volatile int pressedCount = 0;
static volatile int maxSimultaneous = 0;
static volatile uint8_t connectedAddress = 0;

void setup()
{
  // Event prints can burst faster than the default serial TX buffer
  // drains; enlarge it so lines are not truncated mid-flight.
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          connectedAddress = device.address;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connectedAddress = 0;
                             pressedCount = 0;
                             Serial.println("HOST_DISCONNECTED");
                           });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed)
                   {
                     pressedCount++;
                     if (pressedCount > maxSimultaneous)
                     {
                       maxSimultaneous = pressedCount;
                     }
                     Serial.printf("PRESS keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                   else
                   {
                     if (pressedCount > 0)
                     {
                       pressedCount--;
                     }
                     Serial.printf("RELEASE keycode=0x%02x n=%d\n", event.keycode, pressedCount);
                   }
                 });

  usb.onKeyboardState([](const EspUsbHostKeyboardState &state)
                      {
                        int down = 0;
                        for (uint16_t usage = 0; usage <= 0xff; usage++)
                        {
                          if (state.isDown(static_cast<uint8_t>(usage)))
                          {
                            down++;
                          }
                        }
                        Serial.printf("STATE down=%d\n", down);
                      });

  Serial.println("HOST_STATE idle devices=0");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (handleLifecycle(command))
    {
      return;
    }
    if (command == 'i')
    {
      Serial.printf("NKRO bitmap=%u\n", usb.keyboardUsesBitmapReport(connectedAddress) ? 1 : 0);
    }
    else if (command == 'r')
    {
      pressedCount = 0;
      maxSimultaneous = 0;
      Serial.println("RESET");
    }
    else if (command == 'm')
    {
      Serial.printf("MAX n=%d\n", maxSimultaneous);
    }
    else if (command == 'l')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
    }
    else if (command == 'o')
    {
      Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
    }
  }
  delay(1);
}
