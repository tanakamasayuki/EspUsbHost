#include "EspUsbHost.h"

EspUsbHost usb;

static void printModifiers(uint8_t modifiers)
{
  // en: Modifier bits are printed as a compact '+' separated list.
  // ja: モディファイアのビット列を、'+'区切りの短い一覧として表示します。
  static const char *modifierNames[] = {
      "LCTRL", "LSHIFT", "LALT", "LGUI",
      "RCTRL", "RSHIFT", "RALT", "RGUI"};

  if (modifiers == 0)
  {
    Serial.print("none");
    return;
  }

  bool first = true;
  for (int i = 0; i < 8; i++)
  {
    if (modifiers & (1 << i))
    {
      if (!first)
      {
        Serial.print('+');
      }
      Serial.print(modifierNames[i]);
      first = false;
    }
  }
}

static void onKeyboard(const EspUsbHostKeyboardEvent &event)
{
  // en: Dump parsed keyboard fields instead of only printable characters.
  // ja: 表示可能文字だけでなく、パース済みキーボード情報をそのまま表示します。
  const char displayChar = (event.ascii >= 0x20 && event.ascii != 0x7F) ? static_cast<char>(event.ascii) : '.';

  Serial.printf("[%s] keycode=0x%02x ascii=0x%02x(%c) modifiers=",
                event.pressed ? "press  " : "release",
                event.keycode,
                event.ascii,
                displayChar);
  printModifiers(event.modifiers);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);

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

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
