#ifndef __EspUsbHost_H__
#define __EspUsbHost_H__

#include <Arduino.h>
#include <usb/usb_host.h>

class EspUsbHost {
public:
  usb_host_client_handle_t clientHandle;
  usb_device_handle_t deviceHandle;
  uint32_t eventFlags;
  usb_transfer_t *usbTransfer;

  void begin(void);
  void task(void);

  static void _clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  void _configCallback(const usb_config_desc_t *config_desc);

  virtual void onConfig(const uint8_t bDescriptorType, const uint8_t *p);
};


#endif
