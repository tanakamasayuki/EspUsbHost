// Drive a Mirabox N3 / Ajazz AKP03 family LCD macro pad (STREONOR S6 confirmed,
// 1500:3006) from an ESP32-P4: paint the six key screens and read the keys,
// scene keys and encoders - with no vendor software involved.
//
// The pad is a composite HID device. Interface 0 is vendor-defined and carries the
// pad's own "CRT" protocol; interface 1 is a boot keyboard, which the library claims
// and reports through onKeyboard() as usual. This sketch drives interface 0 through
// sendHIDVendorOutput() and onHIDInput(), so the library needs no additions for it -
// which is why the example lives under examples/HID/.
//
// ESP32-P4 only, and only on its high-speed port: the protocol's OUT endpoint has a
// 1024-byte max packet size, which no full-speed port can stage. That also needs a
// FIFO split the host driver does not use by default - see cfg.fifo below and
// README.md.

#include "EspUsbHost.h"
#include "KeyImage.hpp"
#include "MacroPadN3Device.hpp"

EspUsbHost usb;
n3::MacroPadN3Device pad(usb);
n3::KeyImageEncoder image;

// Backlight, 0..100.
static constexpr uint8_t BRIGHTNESS = 80;

static volatile bool deviceArrived = false;
static bool padReady = false;
static uint32_t arrivedAt = 0;

// The session has to be held open or the pad drops off the bus. The vendor
// application sends its keepalive about every ten seconds; half of that leaves room
// for a packet to be missed.
static constexpr uint32_t KEEPALIVE_INTERVAL_MS = 5000;
static uint32_t lastKeepaliveAt = 0;

// One colour per LCD key, so which key ends up where is visible at a glance.
static const uint32_t KEY_COLORS[n3::KEY_COUNT] = {
    0xd62828, 0xf77f00, 0xfcbf49, 0x2a9d8f, 0x264653, 0x8e44ad};

static void paintKey(uint8_t keyIndex, uint32_t color)
{
  const uint16_t body = n3::KeyImageEncoder::rgb565((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
  const uint16_t border = n3::KeyImageEncoder::rgb565(0xff, 0xff, 0xff);

  image.fill(body);
  // Deliberately asymmetric, so one look at the panel says whether
  // KEY_IMAGE_ROTATION is right: a horizontal bar near the top whose length counts
  // the key, and a square in the bottom-left corner. Sideways bars mean the
  // rotation is a quarter turn out; a bar along the bottom means it is 180 out.
  image.fillRect(0, 0, n3::KEY_IMAGE_WIDTH, 2, border);
  image.fillRect(0, n3::KEY_IMAGE_HEIGHT - 2, n3::KEY_IMAGE_WIDTH, 2, border);
  image.fillRect(0, 0, 2, n3::KEY_IMAGE_HEIGHT, border);
  image.fillRect(n3::KEY_IMAGE_WIDTH - 2, 0, 2, n3::KEY_IMAGE_HEIGHT, border);
  image.fillRect(6, 8, static_cast<uint16_t>(6 * keyIndex), 6, border);
  image.fillRect(6, n3::KEY_IMAGE_HEIGHT - 16, 10, 10, border);

  const uint8_t *jpeg = nullptr;
  size_t length = 0;
  if (!image.encode(jpeg, length))
  {
    Serial.printf("key %u: JPEG encode failed\n", keyIndex);
    return;
  }
  const bool ok = pad.setKeyImage(keyIndex, jpeg, length);
  Serial.printf("key %u: %u byte JPEG %s\n", keyIndex, (unsigned)length, ok ? "sent" : "FAILED");
}

static bool connect()
{
  if (!pad.begin())
  {
    return false;
  }
  Serial.printf("pad ready address=%u interface=%u\n", pad.address(), pad.interfaceNumber());

  // Proof that the device speaks this protocol, before anything is sent to it. The
  // first field is the protocol version: this example assumes V3 packet sizes.
  char version[32] = {};
  if (pad.readFirmwareVersion(version, sizeof(version)))
  {
    Serial.printf("firmware=\"%s\"\n", version);
  }
  else
  {
    Serial.println("firmware read failed - this may not be an N3 family pad");
  }

  pad.onInput([](const n3::InputEvent &event, const uint8_t *raw, size_t rawLength)
              {
    // Runs on the USB task, so this only prints. The raw bytes are printed next to
    // the decoded event so that a control this table does not cover is still visible.
    if (event.keyIndex != 0)
    {
      Serial.printf("key %u %s", event.keyIndex, event.pressed ? "down" : "up");
    }
    else if (event.sceneKey != 0)
    {
      Serial.printf("scene key %u %s", event.sceneKey, event.pressed ? "down" : "up");
    }
    else if (event.encoder != 0 && event.delta != 0)
    {
      Serial.printf("encoder %u turn %+d", event.encoder, event.delta);
    }
    else if (event.encoder != 0)
    {
      Serial.printf("encoder %u %s", event.encoder, event.pressed ? "down" : "up");
    }
    else
    {
      Serial.printf("unknown code=0x%02x state=0x%02x", event.code, event.state);
    }
    Serial.print(" raw=");
    for (size_t i = 0; i < rawLength && i < 16; i++)
    {
      Serial.printf("%02x", raw[i]);
    }
    Serial.println(); });

  // Take the screens over and start the input reports. Everything below depends on
  // this, and so does the keepalive in loop().
  if (!pad.beginSession(BRIGHTNESS))
  {
    Serial.println("session start failed");
    return false;
  }
  lastKeepaliveAt = millis();
  pad.clearAll();

  if (!image.begin())
  {
    Serial.println("JPEG encoder unavailable");
    return false;
  }
  paintAllKeys();

  Serial.println("Press the keys and turn the encoders; events print below.");
  return true;
}

static void paintAllKeys()
{
  for (uint8_t key = 1; key <= n3::KEY_COUNT; key++)
  {
    paintKey(key, KEY_COLORS[key - 1]);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  Serial.println("MacroPad N3 example");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
    Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                  device.address, device.vid, device.pid, device.product);
    deviceArrived = true;
    arrivedAt = millis(); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
    Serial.printf("disconnected address=%u\n", device.address);
    padReady = false;
    pad.end(); });

  // The pad's own keyboard interface, for reference. Its keys report on the vendor
  // interface above, but the pad can also be configured to send keystrokes, and those
  // arrive here.
  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
    if (event.pressed)
    {
      Serial.printf("keyboard keycode=0x%02x modifiers=0x%02x\n", event.keycode, event.modifiers);
    } });

  EspUsbHostConfig cfg;
  // Both are required for this device. The high-speed port is the only one whose
  // FIFO can hold a 1024-byte packet, and the default split caps interrupt OUT at
  // 512 bytes, which makes claiming interface 0 fail with ESP_ERR_NOT_SUPPORTED.
  cfg.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  cfg.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;
  if (!usb.begin(cfg))
  {
    Serial.printf("usb.begin() failed: %s\n", esp_err_to_name(usb.lastError()));
  }
}

void loop()
{
  // Enumeration finishes a moment after the connect callback, so the interface is
  // looked up on a short delay rather than from inside it.
  if (deviceArrived && !padReady && millis() - arrivedAt > 500)
  {
    deviceArrived = false;
    padReady = connect();
    if (!padReady)
    {
      Serial.println("no N3 family pad found on this device");
    }
  }

  if (padReady && millis() - lastKeepaliveAt >= KEEPALIVE_INTERVAL_MS)
  {
    lastKeepaliveAt = millis();
    pad.keepalive();
  }

  delay(10);
}
