// Real transport methods with a fake USB driver: bulk length must survive
// resubmission, and each completed payload must reach assembly without headers.
#include "espusbhost_video_real.h"
#include <cassert>
#include <cstdio>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int USB_TRANSFER_STATUS_COMPLETED = 0;
constexpr int USB_TRANSFER_STATUS_CANCELED = 3;
constexpr int USB_TRANSFER_STATUS_NO_DEVICE = 7;
using usb_transfer_status_t = int;
struct usb_isoc_packet_desc_t
{
  size_t num_bytes = 0;
  size_t actual_num_bytes = 0;
  int status = 0;
};
struct usb_transfer_t
{
  void *context = nullptr;
  int status = USB_TRANSFER_STATUS_COMPLETED;
  uint8_t bEndpointAddress = 0x81;
  uint8_t *data_buffer = nullptr;
  size_t num_bytes = 0;
  size_t actual_num_bytes = 0;
  int num_isoc_packets = 0;
  usb_isoc_packet_desc_t isoc_packet_desc[2];
};
static int submitResult = ESP_OK;
static int submitted = 0;
static int cacheSyncs = 0;
static int freed = 0;
static void usb_host_transfer_free(usb_transfer_t *) { freed++; }
static esp_err_t usb_host_transfer_submit(usb_transfer_t *)
{
  submitted++;
  return submitResult;
}
static void espUsbHostCacheSyncBeforeInTransfer(usb_transfer_t *) { cacheSyncs++; }
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define ESP_LOGW(...) ((void)0)

class EspUsbHost
{
public:
  struct VideoState
  {
    usb_transfer_t *transfers[1] = {};
    bool transferInFlight[1] = {};
    uint8_t submitting = 0;
    uint8_t transferCount = 1;
    bool stopping = false;
    bool streamingActive = true;
    bool bulkSyncPending = false;
    bool frameIdValid = false;
    bool frameId = false;
    uint32_t activePacketBytes = 64;
    EspUsbHostVideoStreamInfo activeStream;
    uint8_t *frameBuffer = nullptr;
    EspUsbHostVideoStats stats;
  };
  struct DeviceState { VideoState *video = nullptr; };
  DeviceState devices_[1];
  VideoState *videoStateFor(DeviceState &device) { return device.video; }
  int videoSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const
  {
    return device.video && device.video->transfers[0] == transfer ? 0 : -1;
  }
  int error = ESP_OK;
  void setLastError(int value) { error = value; }
  std::vector<std::vector<uint8_t>> payloads;
  bool stopDuringAssembly = false;
  bool assemblySawInFlight = false;
  void videoAppendPayload(DeviceState &device, const EspUsbHostVideoPayloadHeader &,
                          const uint8_t *data, size_t length)
  {
    assemblySawInFlight = device.video->transferInFlight[0];
    payloads.emplace_back(data, data + length);
    if (stopDuringAssembly) device.video->stopping = true;
  }
  bool submitVideoTransfer(DeviceState &, uint8_t);
  void handleVideo(DeviceState &, usb_transfer_t *);
  static void videoTransferCallback(usb_transfer_t *);
};

#include "espusbhost_video_transfers_real.h"

int main()
{
  EspUsbHost host;
  EspUsbHost::VideoState video;
  EspUsbHost::DeviceState device{&video};
  uint8_t buffer[128] = {2, 0x82, 11, 22, 33};
  usb_transfer_t transfer;
  transfer.data_buffer = buffer;
  transfer.num_bytes = sizeof(buffer);
  transfer.actual_num_bytes = 5;
  video.transfers[0] = &transfer;
  video.frameBuffer = buffer;
  video.activeStream.isochronous = false;

  assert(host.submitVideoTransfer(device, 0));
  assert(transfer.num_bytes == 128); // Bulk must never be reset to zero.
  assert(video.transferInFlight[0]);
  assert(cacheSyncs == 1);
  assert(host.submitVideoTransfer(device, 0));
  assert(submitted == 1 && cacheSyncs == 1); // Busy buffers must remain untouched.
  video.transferInFlight[0] = false; // The completion callback returns ownership.
  host.handleVideo(device, &transfer);
  assert(host.payloads.size() == 1);
  assert((host.payloads[0] == std::vector<uint8_t>{11, 22, 33}));

  transfer.actual_num_bytes = 0; // ZLP is a boundary, not an invalid header.
  host.handleVideo(device, &transfer);
  assert(host.payloads.size() == 1 && video.stats.headerErrors == 0);
  transfer.actual_num_bytes = 1; // Truncated header must not reach assembly.
  host.handleVideo(device, &transfer);
  assert(host.payloads.size() == 1 && video.stats.headerErrors == 1);

  video.bulkSyncPending = true;
  host.handleVideo(device, &transfer); // Startup fragments are discarded.
  assert(video.stats.headerErrors == 1);
  transfer.actual_num_bytes = 5;
  host.handleVideo(device, &transfer); // EOF from the paused frame.
  assert(host.payloads.size() == 1 && video.bulkSyncPending);
  buffer[1] = 0x83; // New FID: the next frame starts here.
  host.handleVideo(device, &transfer);
  assert(host.payloads.size() == 2 && !video.bulkSyncPending);
  buffer[1] = 0x82;

  // Existing isochronous packet offsets and per-packet failure accounting.
  video.activeStream.isochronous = true;
  transfer.num_isoc_packets = 2;
  assert(host.submitVideoTransfer(device, 0));
  assert(transfer.num_bytes == 128);
  buffer[64] = 2;
  buffer[65] = 0x82;
  buffer[66] = 44;
  transfer.isoc_packet_desc[0].status = 1;
  transfer.isoc_packet_desc[1].actual_num_bytes = 3;
  host.handleVideo(device, &transfer);
  assert(video.stats.packetErrors == 1);
  assert((host.payloads.back() == std::vector<uint8_t>{44}));

  video.transferInFlight[0] = false;
  submitResult = 7;
  assert(!host.submitVideoTransfer(device, 0));
  assert(host.error == 7 && !video.transferInFlight[0]);
  video.stopping = true;
  const int before = submitted;
  assert(!host.submitVideoTransfer(device, 0));
  assert(submitted == before); // Teardown must prevent new driver ownership.

  // Simulate stopping while a completion callback assembles a frame. Its busy
  // flag must stay set until assembly returns, otherwise stop can free live data.
  host.devices_[0] = device;
  host.stopDuringAssembly = true;
  video.stopping = false;
  video.activeStream.isochronous = false;
  video.transferInFlight[0] = true;
  transfer.num_isoc_packets = 0;
  transfer.actual_num_bytes = 5;
  transfer.context = &host;
  EspUsbHost::videoTransferCallback(&transfer);
  assert(host.assemblySawInFlight);
  assert(!video.transferInFlight[0] && submitted == before);

  host.devices_[0].video = nullptr; // A late completion after pool teardown.
  EspUsbHost::videoTransferCallback(&transfer);
  assert(freed == 1);
  puts("video transport checks passed");
}
