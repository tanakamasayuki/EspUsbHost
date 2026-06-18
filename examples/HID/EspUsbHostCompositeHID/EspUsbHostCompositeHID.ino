#include "EspUsbHost.h"

EspUsbHost usb;

static int32_t posX = 0;
static int32_t posY = 0;

static void onKeyboard(const EspUsbHostKeyboardEvent &event)
{
  if (!event.pressed)
  {
    return;
  }

  Serial.printf("keyboard: address=%u iface=%u keycode=0x%02x",
                event.address,
                event.interfaceNumber,
                event.keycode);

  if (event.ascii == '\r')
  {
    Serial.print(" ascii=Enter");
  }
  else if (event.ascii >= 0x20 && event.ascii != 0x7f)
  {
    Serial.printf(" ascii='%c'", (char)event.ascii);
  }

  Serial.println();
}

static void onMouse(const EspUsbHostMouseEvent &event)
{
  if (event.buttonsChanged)
  {
    uint8_t pressed = event.buttons & ~event.previousButtons;
    uint8_t released = event.previousButtons & ~event.buttons;
    if (pressed & ESP_USB_HOST_MOUSE_LEFT)
      Serial.printf("mouse: address=%u iface=%u left press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_LEFT)
      Serial.printf("mouse: address=%u iface=%u left release\n", event.address, event.interfaceNumber);
    if (pressed & ESP_USB_HOST_MOUSE_RIGHT)
      Serial.printf("mouse: address=%u iface=%u right press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_RIGHT)
      Serial.printf("mouse: address=%u iface=%u right release\n", event.address, event.interfaceNumber);
    if (pressed & ESP_USB_HOST_MOUSE_MIDDLE)
      Serial.printf("mouse: address=%u iface=%u middle press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_MIDDLE)
      Serial.printf("mouse: address=%u iface=%u middle release\n", event.address, event.interfaceNumber);
  }

  if (event.moved)
  {
    posX += event.x;
    posY += event.y;
    Serial.printf("mouse: address=%u iface=%u pos: x=%ld y=%ld delta: dx=%d dy=%d wheel=%d\n",
                  event.address,
                  event.interfaceNumber,
                  (long)posX,
                  (long)posY,
                  (int)event.x,
                  (int)event.y,
                  (int)event.wheel);
  }
}

static void onConsumerControl(const EspUsbHostConsumerControlEvent &event)
{
  Serial.printf("consumer: address=%u iface=%u %s usage=0x%04x %s\n",
                event.address,
                event.interfaceNumber,
                event.pressed ? "press" : "release",
                event.usage,
                espUsbHostConsumerControlUsageName(event.usage));
}

static void onSystemControl(const EspUsbHostSystemControlEvent &event)
{
  Serial.printf("system: address=%u iface=%u %s usage=0x%02x %s\n",
                event.address,
                event.interfaceNumber,
                event.pressed ? "press" : "release",
                event.usage,
                espUsbHostSystemControlUsageName(event.usage));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost multiple HID example start");

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onKeyboard(onKeyboard);
  usb.onMouse(onMouse);
  usb.onConsumerControl(onConsumerControl);
  usb.onSystemControl(onSystemControl);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
