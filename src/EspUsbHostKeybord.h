#ifndef __EspUsbHostKeybord_H__
#define __EspUsbHostKeybord_H__

#include "EspUsbHost.h"

class EspUsbHostKeybord : public EspUsbHost {
public:
  bool isReady = false;
  uint8_t interval;
  unsigned long lastCheck;

  void onConfig(const uint8_t bDescriptorType, const uint8_t *p) {
    ESP_LOGI("EspUsbHostKeybord", "bDescriptorType=%d", bDescriptorType);
    switch (bDescriptorType) {
      case USB_B_DESCRIPTOR_TYPE_INTERFACE:
        {
          const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;

          if ((intf->bInterfaceClass == USB_CLASS_HID) && (intf->bInterfaceSubClass == 1) && (intf->bInterfaceProtocol == 1)) {
            esp_err_t err = usb_host_interface_claim(this->clientHandle, this->deviceHandle, intf->bInterfaceNumber, intf->bAlternateSetting);
            if (err != ESP_OK) {
              ESP_LOGI("EspUsbHostKeybord", "usb_host_interface_claim() err=%x", err);
            }
          }
        }
        break;

      case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
        if (this->usbTransfer == NULL) {
          const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
          esp_err_t err;

          if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_INT) {
            ESP_LOGI("EspUsbHostKeybord", "err endpoint->bmAttributes=%x", endpoint->bmAttributes);
            return;
          }

          if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
            err = usb_host_transfer_alloc(8, 0, &this->usbTransfer);
            if (err != ESP_OK) {
              this->usbTransfer = NULL;
              ESP_LOGI("EspUsbHostKeybord", "usb_host_transfer_alloc() err=%x", err);
              return;
            }

            this->usbTransfer->device_handle = this->deviceHandle;
            this->usbTransfer->bEndpointAddress = endpoint->bEndpointAddress;
            this->usbTransfer->callback = this->_onKey;
            this->usbTransfer->context = this;
            interval = endpoint->bInterval;
            isReady = true;
          }
        }
        break;
    }
  };

  static void _onKey(usb_transfer_t *transfer) {
    if (transfer->status == 0 && transfer->actual_num_bytes == 8) {
      EspUsbHostKeybord *usbHost = (EspUsbHostKeybord*)transfer->context;
      usbHost->onKey(transfer);
    }
  };

  virtual void onKey(usb_transfer_t *transfer);

  void task(void) {
    EspUsbHost::task();
    if (this->isReady) {
      unsigned long now = millis();
      if ((now - this->lastCheck) > this->interval) {
        this->lastCheck = now;
        this->usbTransfer->num_bytes = 8;
        esp_err_t err = usb_host_transfer_submit(this->usbTransfer);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
          ESP_LOGI("EspUsbHostKeybord", "usb_host_transfer_submit() err=%x", err);
        }
      }
    }
  }
};

#endif
