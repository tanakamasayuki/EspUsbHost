#include <Arduino.h>
#include <usb/usb_host.h>

static usb_host_client_handle_t clientHandle = nullptr;
static TaskHandle_t hostTaskHandle = nullptr;
static TaskHandle_t clientTaskHandle = nullptr;
static volatile bool running = true;

static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *)
{
  switch (eventMsg->event)
  {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    Serial.printf("NEW_DEV address=%u\n", eventMsg->new_dev.address);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    Serial.println("DEV_GONE");
    break;
  default:
    Serial.printf("client event=%d\n", eventMsg->event);
    break;
  }
}

static void hostTask(void *)
{
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK)
  {
    Serial.printf("usb_host_install failed: %s\n", esp_err_to_name(err));
    vTaskDelete(nullptr);
    return;
  }
  Serial.println("usb_host_install OK");

  while (running)
  {
    uint32_t eventFlags = 0;
    err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      Serial.printf("usb_host_lib_handle_events failed: %s flags=0x%08x\n",
                    esp_err_to_name(err),
                    eventFlags);
    }
  }

  usb_host_uninstall();
  hostTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static void clientTask(void *)
{
  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 10;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = nullptr;

  esp_err_t err = usb_host_client_register(&clientConfig, &clientHandle);
  if (err != ESP_OK)
  {
    Serial.printf("usb_host_client_register failed: %s\n", esp_err_to_name(err));
    vTaskDelete(nullptr);
    return;
  }
  Serial.println("usb_host_client_register OK");

  while (running)
  {
    err = usb_host_client_handle_events(clientHandle, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      Serial.printf("usb_host_client_handle_events failed: %s\n", esp_err_to_name(err));
    }
  }

  usb_host_client_deregister(clientHandle);
  clientHandle = nullptr;
  clientTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHostHubProbe start");

  xTaskCreate(hostTask, "HubProbeHost", 4096, nullptr, 5, &hostTaskHandle);
  delay(50);
  xTaskCreate(clientTask, "HubProbeClient", 4096, nullptr, 5, &clientTaskHandle);
}

void loop()
{
  delay(1000);
}
