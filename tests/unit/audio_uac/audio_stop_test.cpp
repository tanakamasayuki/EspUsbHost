// Production capture stop against a fake controller with explicit halt state.
#include <cassert>
#include <cstdint>
#include <cstdio>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_INVALID_STATE = 1;
constexpr int ESP_ERR_TIMEOUT = 2;
constexpr int ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS = 1000;
static uint32_t ticks = 0;
static int currentTask = 1;
static bool hardwareHalted = false;
static bool completeFlush = true;
static bool *submitted = nullptr;
static int halts = 0, flushes = 0, clears = 0;
static int xTaskGetCurrentTaskHandle() { return currentTask; }
static uint32_t millis() { return ticks; }
static void vTaskDelay(int delay) { ticks += delay; }
#define pdMS_TO_TICKS(value) (value)
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
static int usb_host_endpoint_halt(int, uint8_t)
{
  halts++;
  hardwareHalted = true;
  return ESP_OK;
}
static int usb_host_endpoint_flush(int, uint8_t)
{
  assert(hardwareHalted);
  flushes++;
  if (completeFlush) *submitted = false;
  return ESP_OK;
}
static int usb_host_endpoint_clear(int, uint8_t)
{
  clears++;
  if (!hardwareHalted) return ESP_ERR_INVALID_STATE;
  hardwareHalted = false;
  return ESP_OK;
}

class EspUsbHost
{
public:
  struct AudioInputQueue
  {
    uint8_t submitting = 0;
    bool halted = false;
    bool stopped = true;
  } queue;
  struct EndpointState
  {
    AudioInputQueue *audioInput;
    bool stopping = false;
    bool resubmitPending = true;
    bool recoveryPending = true;
    bool transferSubmitted = false;
    uint8_t address = 0x81;
  } endpoint{&queue};
  struct AudioState
  {
    uint8_t inEndpointAddress = 0x81;
    uint8_t inAlternate = 1;
    uint8_t inInterfaceNumber = 2;
  } audio;
  struct DeviceState { int handle = 1; } device;
  int clientTaskHandle_ = 2;
  int error = ESP_OK;
  int interfaceChanges = 0;
  bool interfaceSucceeds = true;
  DeviceState *findAudioInputDevice(uint8_t) { return &device; }
  AudioState *audioStateFor(DeviceState &) { return &audio; }
  EndpointState *findEndpoint(int, uint8_t) { return &endpoint; }
  void setLastError(int value) { error = value; }
  bool setInterfaceSync(DeviceState &, uint8_t, uint8_t alternate, uint32_t)
  {
    assert(alternate == 0 && endpoint.stopping && !endpoint.transferSubmitted);
    interfaceChanges++;
    return interfaceSucceeds;
  }
  bool audioInputStop(uint8_t);
};

#include "espusbhost_audio_stop_real.h"

int main()
{
  EspUsbHost host;
  submitted = &host.endpoint.transferSubmitted;
  assert(host.audioInputStop(1)); // Never started: no controller commands.
  assert(halts == 0 && clears == 0 && host.interfaceChanges == 0);

  host.queue.stopped = false;
  *submitted = true;
  assert(host.audioInputStop(1));
  assert(host.queue.stopped && !host.queue.halted && !*submitted);
  assert(!host.endpoint.resubmitPending && !host.endpoint.recoveryPending);
  assert(halts == 1 && flushes == 1 && clears == 1 && host.interfaceChanges == 1);
  assert(host.audioInputStop(1)); // Clearing an already active pipe is invalid.
  assert(clears == 1 && host.interfaceChanges == 1);

  // The pipe is already cleared if SET_INTERFACE failed. Retry only that step.
  host.queue.stopped = false;
  *submitted = true;
  host.interfaceSucceeds = false;
  assert(!host.audioInputStop(1));
  assert(!host.queue.stopped && !host.queue.halted && clears == 2);
  host.interfaceSucceeds = true;
  assert(host.audioInputStop(1));
  assert(clears == 2 && host.interfaceChanges == 3);

  // A cancellation timeout keeps the halt and buffers until a later completion.
  host.queue.stopped = false;
  *submitted = true;
  completeFlush = false;
  assert(!host.audioInputStop(1));
  assert(host.error == ESP_ERR_TIMEOUT && host.queue.halted && *submitted);
  assert(clears == 2 && ticks >= 1000);
  *submitted = false; // Late driver completion.
  assert(host.audioInputStop(1));
  assert(clears == 3 && !hardwareHalted && host.queue.stopped);

  currentTask = host.clientTaskHandle_;
  assert(!host.audioInputStop(1)); // Waiting here would deadlock the dispatcher.
  assert(host.error == ESP_ERR_INVALID_STATE && clears == 3);
  puts("capture stop lifecycle checks passed");
}
