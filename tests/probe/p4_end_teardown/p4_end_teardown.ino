#include "EspUsbHost.h"

#include <Preferences.h>

// Which part of a session makes end() fault on an ESP32-P4 high-speed port?
//
// An earlier probe found that end() panics inside the host library's own
// interrupt path (proc_req_callback <- intr_hdlr_main) after a vendor bulk IN
// session, in both the default and the forced-full-speed bus mode -- so the bus
// mode is not the variable. This walks a ladder of increasingly complete
// sessions and reports which one's end() is the first to fault.
//
// The step index is kept in NVS, which a panic reboot does not clear, and is
// advanced *before* the step runs, so a step that faults is not retried: one
// flash walks the whole ladder even though each fault reboots the board.
// (RTC_NOINIT was tried first and left the board producing no output at all on
// this chip -- whatever it faulted on happened before USB CDC was up, so there
// was nothing to read.)
//
// Peer: the board wired to this one's OTG HS port, running
// tests/peer/usb_vendor_read/peer_device (or the same protocol).

EspUsbHost usb;

static constexpr uint16_t PEER_VID = 0x303a;
static constexpr uint16_t PEER_PID = 0x4019;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
static constexpr size_t STREAM_BYTES = 64 * 1024;
static constexpr uint32_t STREAM_TIMEOUT_MS = 10000;

// Bump to restart the ladder after a reflash: the stored index is ignored when
// it was written by a different build of this sketch.
static constexpr uint32_t LADDER_BUILD = 10;
static Preferences ladderStore;
static uint32_t ladderStep = 0;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static volatile size_t streamBytes = 0;
static volatile bool streamArmed = false;

struct Step
{
  const char *name;
  bool openVendor;      // vendorOpen()
  size_t readTransfer;  // 0 = endpoint packet size
  bool useQueue;        // vendorReadQueueBegin()
  bool stream;          // pull data before tearing down
  bool endQueueFirst;   // vendorReadQueueEnd() before end()
  bool reopen;          // begin() again after end(), on the same port
};

static const Step STEPS[] = {
    {"begin_end", false, 0, false, false, false, false},
    {"open_default", true, 0, false, false, false, false},
    {"open_large", true, 8192, false, false, false, false},
    {"open_large_stream", true, 8192, false, true, false, false},
    {"queue_stream_endqueue", true, 8192, true, true, true, false},
    {"queue_stream_no_endqueue", true, 8192, true, true, false, false},
    // Restarting the host after end() is what the EspUsbDevice P4 loopback role
    // reversal does, and what it aborts on. Kept as separate rungs so the plain
    // end() rungs above keep meaning what they meant.
    {"begin_end_begin", false, 0, false, false, false, true},
    {"open_stream_end_begin", true, 8192, false, true, false, true},
    {"queue_stream_end_begin", true, 8192, true, true, true, true},
};
static constexpr size_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

static bool waitConnected()
{
  const uint32_t deadline = millis() + CONNECT_TIMEOUT_MS;
  while (!connected && millis() < deadline)
  {
    delay(10);
  }
  return connected;
}

static bool pullStream()
{
  streamArmed = false;
  delay(100);
  streamBytes = 0;
  streamArmed = true;

  const size_t bytes = STREAM_BYTES;
  const uint8_t request[5] = {'S',
                              static_cast<uint8_t>(bytes & 0xff),
                              static_cast<uint8_t>((bytes >> 8) & 0xff),
                              static_cast<uint8_t>((bytes >> 16) & 0xff),
                              static_cast<uint8_t>((bytes >> 24) & 0xff)};
  if (!usb.vendorWrite(request, sizeof(request), deviceAddress))
  {
    streamArmed = false;
    return false;
  }
  const uint32_t deadline = millis() + STREAM_TIMEOUT_MS;
  while (streamBytes < bytes && millis() < deadline)
  {
    delay(1);
  }
  streamArmed = false;
  return streamBytes >= bytes;
}

static void runStep(const Step &step)
{
  EspUsbHostConfig config;
  config.port = ESP_USB_HOST_PORT_HIGH_SPEED;

  connected = false;
  deviceAddress = 0;

  if (!usb.begin(config))
  {
    Serial.printf("STEP_FAIL name=%s reason=begin error=%s\n", step.name, usb.lastErrorName());
    return;
  }
  if (!waitConnected())
  {
    Serial.printf("STEP_FAIL name=%s reason=no_device\n", step.name);
    usb.end();
    return;
  }

  bool opened = false;
  bool queued = false;
  bool streamed = false;
  if (step.openVendor)
  {
    opened = usb.vendorOpen(deviceAddress, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, step.readTransfer);
  }
  if (opened && step.useQueue)
  {
    queued = usb.vendorReadQueueBegin(2, step.readTransfer, deviceAddress);
  }
  if (opened && step.stream)
  {
    streamed = pullStream();
  }
  if (queued && step.endQueueFirst)
  {
    usb.vendorReadQueueEnd(deviceAddress);
  }

  Serial.printf("STEP_STATE name=%s opened=%u queued=%u streamed=%u\n",
                step.name, opened ? 1 : 0, queued ? 1 : 0, streamed ? 1 : 0);
  Serial.flush();
  delay(50);

  // Everything above is setup. This is the call under test.
  Serial.printf("STEP_END_ENTER name=%s\n", step.name);
  Serial.flush();
  usb.end();
  Serial.printf("STEP_END_OK name=%s\n", step.name);
  Serial.flush();
  delay(500);

  if (!step.reopen)
  {
    return;
  }

  // The second begin() on the same port. On the ESP32-P4 end() cuts root port
  // power, so this is where a port left unpowered, or state the teardown did not
  // reset, shows up.
  Serial.printf("STEP_REOPEN_ENTER name=%s\n", step.name);
  Serial.flush();
  const bool reopened = usb.begin(config);
  Serial.printf("STEP_REOPEN name=%s ok=%u error=%s\n",
                step.name, reopened ? 1 : 0, usb.lastErrorName());
  Serial.flush();
  if (reopened)
  {
    // Say whether the restarted host can still see the device, not just that
    // begin() returned true: an unpowered root port enumerates nothing.
    connected = false;
    const bool sawDevice = waitConnected();
    Serial.printf("STEP_REOPEN_DEVICE name=%s connected=%u\n", step.name, sawDevice ? 1 : 0);
    Serial.flush();
    usb.end();
  }
  Serial.printf("STEP_REOPEN_OK name=%s\n", step.name);
  Serial.flush();
  delay(500);
}

void setup()
{
  Serial.begin(115200);
  delay(2500);

  ladderStore.begin("p4ladder", false);
  if (ladderStore.getUInt("build", 0) != LADDER_BUILD)
  {
    ladderStore.putUInt("build", LADDER_BUILD);
    ladderStore.putUInt("step", 0);
    ladderStep = 0;
    Serial.println("TEST_BEGIN p4_end_teardown_probe");
  }
  else
  {
    ladderStep = ladderStore.getUInt("step", 0);
    Serial.println("TEST_RESUME p4_end_teardown_probe");
  }

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          if (device.vid == PEER_VID && device.pid == PEER_PID)
                          {
                            deviceAddress = device.address;
                            connected = true;
                          }
                        });
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     if (streamArmed)
                     {
                       streamBytes += data.length;
                     }
                   });

  while (ladderStep < STEP_COUNT)
  {
    const size_t index = ladderStep;
    ladderStep = index + 1; // advance first: a step that faults is not retried
    ladderStore.putUInt("step", ladderStep);
    Serial.printf("STEP_BEGIN index=%u name=%s\n",
                  static_cast<unsigned>(index), STEPS[index].name);
    Serial.flush();
    runStep(STEPS[index]);
  }

  Serial.println("LADDER_DONE");
}

void loop()
{
  delay(1000);
}
