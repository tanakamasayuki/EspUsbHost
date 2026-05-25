#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static uint32_t lastDeviceEventMs = 0;
static uint32_t lastPrintMs = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 10000;

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHostDeviceInfo start");
  Serial.println("Printing connected devices and endpoint channel estimates every 10 seconds.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          lastDeviceEventMs = millis();
                          Serial.printf("CONNECTED address=%u\n", device.address); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             lastDeviceEventMs = millis();
                             Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                           device.address,
                                           device.vid,
                                           device.pid); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
  lastPrintMs = millis() - PRINT_INTERVAL_MS;
}

void loop()
{
  while (Serial.available() > 0)
  {
    Serial.read();
  }

  const uint32_t now = millis();
  if (now - lastPrintMs >= PRINT_INTERVAL_MS && now - lastDeviceEventMs > 500)
  {
    lastPrintMs = now;
    usb.printAllDeviceInfo();
  }
  delay(10);
}
