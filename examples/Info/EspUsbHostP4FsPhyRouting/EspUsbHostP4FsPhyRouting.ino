#include <Arduino.h>
#include <EspUsbHost.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "EspUsbHostP4FsPhyRouting requires ESP32-P4"
#endif

#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println("Routing USB OTG FS to GPIO24/GPIO25 in 3 seconds.");
  Serial.println("USB Serial/JTAG on GPIO24/GPIO25 will disconnect.");
  delay(3000);

  // Route USB OTG FS to FSLS PHY0: GPIO24=D-, GPIO25=D+.
  // USB Serial/JTAG moves to FSLS PHY1: GPIO26=D-, GPIO27=D+.
  // This is a runtime override and does not modify eFuse.
  usb_wrap_ll_phy_select(&USB_WRAP, 0);

  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_FULL_SPEED;

  if (!usb.begin(config))
  {
    // This is visible only when Serial is available through another connection,
    // such as an external USB-to-UART bridge.
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
