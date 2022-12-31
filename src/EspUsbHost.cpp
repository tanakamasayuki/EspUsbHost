#include "EspUsbHost.h"

void EspUsbHost::begin(void) {
  const usb_host_config_t config = {
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  esp_err_t err = usb_host_install(&config);
  if (err != ESP_OK) {
    ESP_LOGI("EspUsbHost", "usb_host_install err=%x", err);
  }

  const usb_host_client_config_t client_config = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
      .client_event_callback = this->_clientEventCallback,
      .callback_arg = this,
    }
  };
  err = usb_host_client_register(&client_config, &this->clientHandle);
  if (err != ESP_OK) {
    ESP_LOGI("EspUsbHost", "usb_host_client_register() err=%x", err);
  }
}

void EspUsbHost::_clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg) {
  EspUsbHost *usbHost = (EspUsbHost *)arg;

  esp_err_t err;
  switch (eventMsg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      ESP_LOGI("EspUsbHost", "USB_HOST_CLIENT_EVENT_NEW_DEV new_dev.address=%d", eventMsg->new_dev.address);
      err = usb_host_device_open(usbHost->clientHandle, eventMsg->new_dev.address, &usbHost->deviceHandle);
      if (err != ESP_OK) {
        ESP_LOGI("EspUsbHost", "usb_host_device_open() err=%x", err);
      }

      usb_device_info_t dev_info;
      err = usb_host_device_info(usbHost->deviceHandle, &dev_info);
      if (err != ESP_OK) {
        ESP_LOGI("EspUsbHost", "usb_host_device_info() err=%x", err);
      }

      const usb_device_desc_t *dev_desc;
      err = usb_host_get_device_descriptor(usbHost->deviceHandle, &dev_desc);
      if (err != ESP_OK) {
        ESP_LOGI("EspUsbHost", "usb_host_get_device_descriptor() err=%x", err);
      }

      const usb_config_desc_t *config_desc;
      err = usb_host_get_active_config_descriptor(usbHost->deviceHandle, &config_desc);
      if (err != ESP_OK) {
        ESP_LOGI("EspUsbHost", "usb_host_get_active_config_descriptor() err=%x", err);
      }

      usbHost->_configCallback(config_desc);
      break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      ESP_LOGI("EspUsbHost", "USB_HOST_CLIENT_EVENT_DEV_GONE dev_gone.dev_hdl=%x", eventMsg->dev_gone.dev_hdl);
      break;

    default:
      ESP_LOGI("EspUsbHost", "clientEventCallback() default %d", eventMsg->event);
      break;
  }
}

void EspUsbHost::_configCallback(const usb_config_desc_t *config_desc) {
  const uint8_t *p = &config_desc->val[0];
  uint8_t bLength;
  for (int i = 0; i < config_desc->wTotalLength; i += bLength, p += bLength) {
    bLength = *p;
    if ((i + bLength) <= config_desc->wTotalLength) {
      const uint8_t bDescriptorType = *(p + 1);
      this->onConfig(bDescriptorType, p);
    } else {
      return;
    }
  }
}

void EspUsbHost::task(void) {
  esp_err_t err = usb_host_lib_handle_events(1, &this->eventFlags);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    ESP_LOGI("EspUsbHost", "usb_host_lib_handle_events() err=%x eventFlags=%x", err, this->eventFlags);
  }

  err = usb_host_client_handle_events(this->clientHandle, 1);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    ESP_LOGI("EspUsbHost", "usb_host_client_handle_events() err=%x", err);
  }
}
