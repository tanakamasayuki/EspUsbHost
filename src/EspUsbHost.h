#ifndef ESP_USB_HOST_H
#define ESP_USB_HOST_H

#include <Arduino.h>
#include <FS.h>
#include <functional>
#include <memory>
#include <usb/usb_host.h>
#include <class/hid/hid.h>

#include "EspUsbHostCcidAtr.h"
#include "EspUsbHostHidLayout.h"

// lwIP / esp_netif integration for networkAttachNetif() is optional and only
// compiled when the esp_netif headers are available in the build.
#if __has_include(<esp_netif.h>)
#define ESP_USB_HOST_HAS_ESP_NETIF 1
#endif

#if __has_include(<rom/usb/usb_common.h>)
#include <rom/usb/usb_common.h>
#else
#define USB_DEVICE_DESC 0x01
#define USB_CONFIGURATION_DESC 0x02
#define USB_STRING_DESC 0x03
#define USB_INTERFACE_DESC 0x04
#define USB_ENDPOINT_DESC 0x05
#define USB_INTERFACE_ASSOC_DESC 0x0B
#define USB_HID_DESC 0x21
#define USB_HID_REPORT_DESC 0x22
#endif

enum EspUsbHostKeyboardLayout : uint16_t
{
  ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW = 0x0404,
  ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK = 0x0406,
  ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE = 0x0407,
  ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US = 0x0409,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI = 0x040B,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR = 0x040C,
  ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU = 0x040E,
  ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT = 0x0410,
  ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP = 0x0411,
  ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR = 0x0412,
  ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL = 0x0413,
  ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO = 0x0414,
  ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR = 0x0416,
  ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE = 0x041D,
  ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN = 0x0804,
  ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB = 0x0809,
  ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT = 0x0816,
  ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES = 0x0C0A,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH = 0x100C,
};

enum EspUsbHostPort
{
  ESP_USB_HOST_PORT_DEFAULT = 0,
  ESP_USB_HOST_PORT_HIGH_SPEED,
  ESP_USB_HOST_PORT_FULL_SPEED,
};

static constexpr uint8_t ESP_USB_HOST_MOUSE_LEFT = 0x01;
static constexpr uint8_t ESP_USB_HOST_MOUSE_RIGHT = 0x02;
static constexpr uint8_t ESP_USB_HOST_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t ESP_USB_HOST_MOUSE_BACK = 0x08;
static constexpr uint8_t ESP_USB_HOST_MOUSE_FORWARD = 0x10;

static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_INPUT = 0x01;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT = 0x02;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_FEATURE = 0x03;

static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK = 0x01;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK = 0x02;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK = 0x04;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_COMPOSE = 0x08;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_KANA = 0x10;

static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_GAMEPAD = 0x03;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_VENDOR = 0x06;
static constexpr size_t ESP_USB_HOST_GAMEPAD_MAX_REPORT_BYTES = 64;
static constexpr size_t ESP_USB_HOST_MAX_HID_INPUT_FIELDS = 96;
static constexpr size_t ESP_USB_HOST_MAX_HID_EVENT_FIELDS = 64;
// HID Usage Page for Keyboard/Keypad, and the widest NKRO key bitmap we decode
// (256 usages = 32 bytes; NKRO keyboards typically expose 0x00-0xDF = 28 bytes).
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_PAGE_KEYBOARD = 0x0007;
static constexpr uint16_t ESP_USB_HOST_HID_USAGE_PAGE_LED = 0x0008;
static constexpr size_t ESP_USB_HOST_NKRO_BITMAP_MAX_BYTES = 32;

static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF = 0x01;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_STANDBY = 0x02;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST = 0x03;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK = 0x00b5;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK = 0x00b6;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE = 0x00cd;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_MUTE = 0x00e2;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP = 0x00e9;
static constexpr uint16_t ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN = 0x00ea;
static constexpr uint8_t ESP_USB_HOST_ANY_ADDRESS = 0xff;
// Serial-port selector for the CDC APIs: the first ready port of the selected
// device. Ports are numbered from 0 in the order their CDC functions appear in
// the configuration descriptor, which is the same order the device side
// registered them in.
static constexpr uint8_t ESP_USB_HOST_ANY_PORT = 0xff;
using EspUsbHostListenerId = uint32_t;
static constexpr EspUsbHostListenerId ESP_USB_HOST_INVALID_LISTENER_ID = 0;
#ifndef ESP_USB_HOST_MAX_LISTENERS_PER_EVENT
#define ESP_USB_HOST_MAX_LISTENERS_PER_EVENT 4
#endif
// Device lifecycle (connect / disconnect) has its own capacity because the
// number of listeners it needs grows differently from the parsed-input events.
// An input event is watched by however many observers care about that one
// event, which plateaus; lifecycle is watched by *every* subsystem that tracks
// devices, so the count scales with the number of subsystems built on the
// stack. Sharing one macro would force either an input-event slot count nobody
// needs or a lifecycle count that overflows.
#ifndef ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS
#define ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS 8
#endif
// Maximum number of concurrently-tracked USB devices. Each slot is a sizable
// static DeviceState (several KB — RX ring, NTB reassembly buffer, HID field
// tables, etc.), so this constant dominates the library's static RAM use. The
// ESP32-S2 has far less internal RAM than the S3/P4, so it defaults to fewer
// slots to fit. Override for any target by defining ESP_USB_HOST_MAX_DEVICES
// before this header is compiled, e.g. build flag -DESP_USB_HOST_MAX_DEVICES=4.
#ifndef ESP_USB_HOST_MAX_DEVICES
#if defined(CONFIG_IDF_TARGET_ESP32S2)
#define ESP_USB_HOST_MAX_DEVICES 3
#else
#define ESP_USB_HOST_MAX_DEVICES 8
#endif
#endif
static constexpr size_t ESP_USB_HOST_MAX_INTERFACES = 16;
static constexpr size_t ESP_USB_HOST_MAX_ENDPOINTS = 16;
static constexpr size_t ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS = 8;
static constexpr size_t ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE = 512;
static constexpr size_t ESP_USB_HOST_KEYBOARD_BITMAP_SIZE = 32;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_STREAMS = 8;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES = 4;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS = 4;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS = 8;
// UAC2 clock entities and the terminal -> clock links needed to resolve which
// Clock Source carries a streaming interface's sample rate.
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES = 4;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_TERMINALS = 8;
// USB Video (UVC) format/frame combinations tracked per device, and the number
// of discrete frame intervals kept for each. A camera that advertises more of
// either is not rejected: the extra entries are dropped and the ones kept are
// still startable.
static constexpr size_t ESP_USB_HOST_MAX_VIDEO_STREAMS = 8;
static constexpr size_t ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS = 8;
// Alternate settings of a VideoStreaming interface kept per device. Each one is
// a different isochronous bandwidth the camera offers for the same formats, and
// the right one is only known after Probe/Commit reports
// dwMaxPayloadTransferSize, so they all have to be remembered during
// enumeration.
static constexpr size_t ESP_USB_HOST_MAX_VIDEO_ALTERNATES = 12;
// Isochronous packets in one streaming transfer. The transfer is resubmitted
// from its own completion, so this is how long the camera may keep sending
// while the host is between submits. Isochronous packets are never retried, so
// a transfer that is too short loses image data outright; 16 covers 2 ms at
// full speed and 2 ms at high speed.
// Whether the host controller can run a high-bandwidth isochronous endpoint --
// two or three transactions in one service interval, which is how a real webcam
// asks for more than 1024 bytes per microframe.
//
// Zero on arduino-esp32 3.3.11 (ESP-IDF 5.4), and that is a missing feature
// rather than a setting. The DWC2 register field exists (HCCHAR.ec, the Multi
// Count), but `usb_dwc_hal_ep_char_t` has nowhere to carry it -- it holds
// `mps: 11` and nothing else -- and no LL helper writes it, so it keeps its reset
// value of one transaction per interval.
//
// It matters because getting it wrong is silent. A camera on such an alternate
// sends three transactions per interval and the host takes one, so frames arrive
// short with no error reported anywhere. Rather than let that happen, the host
// counts only what it can actually take (see VideoAlternateState::usablePayload)
// and prefers an alternate it can use in full.
//
// To re-check on a newer core: look for a multiplier field in
// `usb_dwc_hal_ep_char_t` and for an LL setter that writes HCCHAR's `ec`. Define
// this to 1 in the sketch's build_opt.h once both are there.
#ifndef ESP_USB_HOST_ISOC_HIGH_BANDWIDTH_SUPPORTED
#define ESP_USB_HOST_ISOC_HIGH_BANDWIDTH_SUPPORTED 0
#endif

static constexpr int ESP_USB_HOST_VIDEO_ISOC_PACKETS = 8;
// Streaming transfers kept in flight at once. One is not enough: an isochronous
// packet that arrives while no transfer is queued is gone, and with a single
// transfer the endpoint is unqueued for the whole time between its completion
// callback and its resubmit. Measured against an EspUsbDevice camera, that gap
// cost about one packet per transfer -- every frame lost a slice exactly
// ESP_USB_HOST_VIDEO_ISOC_PACKETS packets into it. Four transfers cover 32 frame
// intervals, which is longer than any gap the client task introduces.
static constexpr size_t ESP_USB_HOST_MAX_VIDEO_TRANSFERS = 4;
// EspUsbHostCdcSerial objects that can be attached to one host at a time. Eight
// because a single device can now publish up to seven CDC ports and a sketch may
// want a Stream on each of them, with room left for a second device. Each slot is
// one pointer, so the array costs 32 bytes for the whole host.
static constexpr size_t ESP_USB_HOST_MAX_CDC_SERIALS = 8;
// CDC-ACM serial ports tracked per device. What actually caps this is the host
// controller's channel count, not the array: the library claims only the data
// interface of a CDC function and drives the control interface over EP0, so a
// port costs two channels (bulk IN + bulk OUT) and one more goes to the device's
// EP0. soc/usb_dwc_cfg.h gives eight channels on the S2, the S3 and the P4's
// full-speed controller, and sixteen on the P4's high-speed one, which works out
// to three ports and seven ports. The array is sized to those maxima so there is
// nothing to configure: a port is 32 bytes, so even the seven-port array costs
// less static RAM than the old single-port implementation did.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define ESP_USB_HOST_MAX_SERIAL_PORTS 7
#else
#define ESP_USB_HOST_MAX_SERIAL_PORTS 3
#endif
static constexpr size_t ESP_USB_HOST_MAX_NETWORK_INTERFACES = 4;
// Preferred bulk-IN NTB receive buffer. Matches TinyUSB's default
// CFG_TUD_NCM_IN_NTB_MAX_SIZE (3200) so a whole device->host NTB fits in one
// transfer. NCM devices may batch several datagrams into one NTB and are
// entitled to fill their own advertised dwNtbInMaxSize, so the size actually
// used per device is negotiated at open time (SET_NTB_INPUT_SIZE) and reported
// as EspUsbHostNetworkStats::ntbInSize -- it is rounded down to a multiple of
// the endpoint's max packet size, because ESP-IDF wants IN transfer lengths to
// be an integer multiple of MPS (3200 is 50 x 64 but not a multiple of 512, so
// a high-speed link lands on 3072).
static constexpr size_t ESP_USB_HOST_NETWORK_NTB_IN_MAX = 3200;
// Upper bound on the receive buffer we are willing to allocate for a device
// that will not accept SET_NTB_INPUT_SIZE. Such a device keeps its own maximum,
// so the buffer has to follow it or its larger NTBs are unreadable. NCM 1.0
// caps a 16-bit NTB at 0xffff; this is the practical ceiling we pay DMA memory
// for. Override with -DESP_USB_HOST_NETWORK_NTB_IN_LIMIT=... if a device needs
// more.
#ifndef ESP_USB_HOST_NETWORK_NTB_IN_LIMIT
#define ESP_USB_HOST_NETWORK_NTB_IN_LIMIT 16384
#endif
// wBlockLength and the negotiated size are 16-bit, so an override above this
// would wrap to a tiny (or zero) buffer instead of a large one.
static_assert(ESP_USB_HOST_NETWORK_NTB_IN_LIMIT > 0 &&
                  ESP_USB_HOST_NETWORK_NTB_IN_LIMIT <= 0xffff,
              "ESP_USB_HOST_NETWORK_NTB_IN_LIMIT must fit in a 16-bit NTB length");
// Per-device raw RX ring for networkReadFrame() (frames stored as [uint16 len][payload]).
static constexpr size_t ESP_USB_HOST_NETWORK_RX_RING_SIZE = 4096;
// Largest Ethernet frame we accept/transmit (CDC ECM/NCM wMaxSegmentSize default).
static constexpr size_t ESP_USB_HOST_NETWORK_MAX_FRAME = 1514;
static constexpr size_t ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS = 4;
// Per-device vendor bulk IN ring. Overflow drops the oldest byte, so a device
// that bursts faster than the sketch drains it needs a larger ring. Overriding
// this is a last resort (see README): the value must reach every translation
// unit, so it goes in the sketch's build_opt.h as
// -DESP_USB_HOST_VENDOR_RX_BUFFER_SIZE=..., never as a #define in the sketch,
// and a stale build cache can silently keep the old value.
#ifndef ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE
#define ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE 512
#endif
static_assert(ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE >= 2,
              "ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE must leave room for at least one byte");
// Default size of the EspUsbHostCdcSerial receive ring. Prefer the runtime
// setRxBufferSize(): it needs no build configuration and is the supported way to
// change this. Moving the default is only for sketches that cannot call it, and
// then it belongs in the sketch's build_opt.h, with the same caveats as above.
#ifndef ESP_USB_HOST_CDC_RX_BUFFER_SIZE
#define ESP_USB_HOST_CDC_RX_BUFFER_SIZE 512
#endif
static_assert(ESP_USB_HOST_CDC_RX_BUFFER_SIZE >= 2,
              "ESP_USB_HOST_CDC_RX_BUFFER_SIZE must leave room for at least one byte");
static constexpr uint32_t ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS = 5000;
static constexpr uint32_t ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS = 1000;
static constexpr uint32_t ESP_USB_HOST_VIDEO_CONTROL_DEFAULT_TIMEOUT_MS = 1000;
static constexpr uint32_t ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS = 1000;
// Upper bound for vendorWriteQueueBegin(depth, ...). Each slot holds one
// preallocated transfer, so the practical depth is limited by DMA memory rather
// than by this constant.
static constexpr size_t ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH = 8;
// Upper bound for vendorReadQueueBegin(depth, ...). Each slot holds one
// preallocated IN transfer, so the practical depth is limited by DMA memory
// rather than by this constant.
static constexpr size_t ESP_USB_HOST_VENDOR_READ_QUEUE_MAX_DEPTH = 8;
// Same bound for serialWriteQueueBegin(depth, ...). The CDC data OUT endpoint is
// bulk as well, so the queue has the same shape as the vendor one.
static constexpr size_t ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH = 8;
// How long sendSerial() waits for a free queue slot when the asynchronous queue
// is active. Only applies off the USB client task, where waiting can progress.
static constexpr uint32_t ESP_USB_HOST_SERIAL_WRITE_DEFAULT_TIMEOUT_MS = 1000;
// CCID message buffer, allocated per device while a CCID interface is open. A
// short-APDU reader reports dwMaxCCIDMessageLength around 271 bytes, so 512 has
// room for the 10-byte header plus a full response. Override with a build flag
// (-DESP_USB_HOST_CCID_BUFFER_SIZE=...) for readers that need more.
#ifndef ESP_USB_HOST_CCID_BUFFER_SIZE
#define ESP_USB_HOST_CCID_BUFFER_SIZE 512
#endif
// Largest ATR an ISO 7816 card can return.
static constexpr size_t ESP_USB_HOST_CCID_MAX_ATR = 33;
static constexpr uint32_t ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS = 5000;

// How the host controller splits its hardware FIFO between the three staging
// areas, in lines of 4 bytes. The split caps the largest endpoint the host can
// open: IN endpoints are limited to (rxFifoLines - 2) * 4, control/bulk OUT to
// nptxFifoLines * 4, and interrupt/isochronous OUT to ptxFifoLines * 4.
//
// Leave every field 0 to keep the driver default, which on a high-speed port is
// rx = total - 384, nptx = 256 and ptx = 128 lines: 1024 bytes for bulk OUT but
// only 512 for periodic OUT. A device with an interrupt OUT endpoint larger than
// that (a high-speed vendor HID display panel, for instance) fails to claim with
// ESP_ERR_NOT_SUPPORTED and needs ptxFifoLines raised.
//
// The total must fit the controller's FIFO: 1024 lines on the ESP32-P4
// high-speed port, 256 lines on a full-speed port. A larger total, or a zero
// rxFifoLines or nptxFifoLines, is rejected by begin() with ESP_ERR_INVALID_SIZE
// or ESP_ERR_INVALID_ARG in lastError(). Requires arduino-esp32 3.3.0 or newer
// (ESP-IDF 5.5); older cores log a warning and use the driver default.
struct EspUsbHostFifoConfig
{
  uint32_t rxFifoLines = 0;
  uint32_t nptxFifoLines = 0;
  uint32_t ptxFifoLines = 0;
};

// Room for a 1024-byte interrupt OUT endpoint while keeping 512-byte bulk OUT
// and IN endpoints usable. 668 of the 1024 lines an ESP32-P4 high-speed port
// has, so it is only valid on that port.
static constexpr EspUsbHostFifoConfig ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT = {260, 128, 280};

struct EspUsbHostConfig
{
  uint32_t taskStackSize = 8192;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
  EspUsbHostPort port = ESP_USB_HOST_PORT_DEFAULT;
  EspUsbHostFifoConfig fifo = {};
  // Experimental, ESP32-P4 HS port only. Prevent HS negotiation so the root
  // bus, including a high-speed-capable hub, runs at full speed. This avoids
  // split transactions/TTs, which the P4 HS DWC and ESP-IDF hub stack do not
  // support. Core-error recovery can reset this setting; see
  // docs/p4-hs-port-fs-only-hub.ja.md before using it outside a probe.
  bool experimentalForceFullSpeed = false;
};

enum EspUsbHostSerialParity : uint8_t
{
  ESP_USB_HOST_SERIAL_PARITY_NONE = 0,
  ESP_USB_HOST_SERIAL_PARITY_ODD,
  ESP_USB_HOST_SERIAL_PARITY_EVEN,
  ESP_USB_HOST_SERIAL_PARITY_MARK,
  ESP_USB_HOST_SERIAL_PARITY_SPACE,
};

enum EspUsbHostSerialStopBits : uint8_t
{
  ESP_USB_HOST_SERIAL_STOP_BITS_1 = 0,
  ESP_USB_HOST_SERIAL_STOP_BITS_1_5,
  ESP_USB_HOST_SERIAL_STOP_BITS_2,
};

struct EspUsbHostSerialConfig
{
  uint32_t baud = 115200;
  uint8_t dataBits = 8;
  EspUsbHostSerialParity parity = ESP_USB_HOST_SERIAL_PARITY_NONE;
  EspUsbHostSerialStopBits stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_1;
};

struct EspUsbHostDeviceInfo
{
  uint8_t address = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
  uint8_t parentAddress = 0;
  uint8_t portId = 0;
  usb_speed_t speed = USB_SPEED_FULL;
  uint16_t usbVersion = 0;
  uint16_t deviceVersion = 0;
  uint8_t deviceClass = 0;
  uint8_t deviceSubClass = 0;
  uint8_t deviceProtocol = 0;
  uint8_t maxPacketSize0 = 0;
  uint8_t configurationValue = 0;
  uint8_t configurationAttributes = 0;
  uint8_t configurationMaxPower = 0;
  uint8_t configurationInterfaceCount = 0;
  uint16_t configurationTotalLength = 0;
  bool supported = false;
  bool isHub = false;
};

struct EspUsbHostHubInfo
{
  uint8_t address = 0;
  uint8_t portCount = 0;
  uint16_t characteristics = 0;
  bool gangedPowerSwitching = false;
  bool perPortPowerSwitching = false;
  bool noPowerSwitching = false;
  bool compound = false;
  bool gangedOverCurrent = false;
  bool perPortOverCurrent = false;
  bool noOverCurrent = false;
  uint16_t powerOnToPowerGoodMs = 0;
  uint8_t controllerCurrentMa = 0;
  uint8_t descriptorLength = 0;
  uint8_t rawDescriptor[32] = {};
};

struct EspUsbHostDeviceProbeInfo
{
  uint8_t address = 0;
  bool openOk = false;
  bool deviceInfoOk = false;
  bool deviceDescriptorOk = false;
  bool configDescriptorOk = false;
  bool hubDescriptorOk = false;
  uint8_t parentAddress = 0;
  uint8_t parentPort = 0;
  uint8_t speed = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  uint8_t deviceClass = 0;
  uint8_t deviceSubClass = 0;
  uint8_t deviceProtocol = 0;
  uint8_t interfaceCount = 0;
  bool configHasHubInterface = false;
  EspUsbHostHubInfo hub;
};

struct EspUsbHostInterfaceInfo
{
  uint8_t number = 0;
  uint8_t alternate = 0;
  uint8_t interfaceClass = 0;
  uint8_t interfaceSubClass = 0;
  uint8_t interfaceProtocol = 0;
  uint8_t endpointCount = 0;
  bool claimed = false;
  bool claimAttempted = false;
  esp_err_t claimResult = ESP_OK;
};

struct EspUsbHostEndpointInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t attributes = 0;
  uint16_t maxPacketSize = 0;
  uint8_t interval = 0;
};

enum EspUsbHostNetworkProtocol : uint8_t
{
  ESP_USB_HOST_NETWORK_PROTOCOL_NONE = 0,
  ESP_USB_HOST_NETWORK_PROTOCOL_CDC_ECM,
  ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM,
};

struct EspUsbHostNetworkInterfaceInfo
{
  uint8_t address = 0;
  uint8_t configurationValue = 0;
  EspUsbHostNetworkProtocol protocol = ESP_USB_HOST_NETWORK_PROTOCOL_NONE;
  uint8_t controlInterfaceNumber = 0xff;
  uint8_t controlInterfaceAlternate = 0;
  uint8_t dataInterfaceNumber = 0xff;
  uint8_t dataInterfaceAlternate = 0;
  uint8_t macAddressStringIndex = 0;
  uint16_t maxSegmentSize = 0;
  // NCM functional descriptor (bDescriptorSubtype 0x1a): bmNetworkCapabilities.
  // Bit 3 tells us whether the device implements SET/GET_NTB_INPUT_SIZE, i.e.
  // whether the host can cap how large a device->host NTB may get.
  uint8_t networkCapabilities = 0;
  uint16_t ncmVersion = 0;
  uint8_t notificationEndpoint = 0;
  uint16_t notificationMaxPacketSize = 0;
  uint8_t inEndpoint = 0;
  uint8_t outEndpoint = 0;
  uint16_t inMaxPacketSize = 0;
  uint16_t outMaxPacketSize = 0;

  bool complete() const
  {
    return protocol != ESP_USB_HOST_NETWORK_PROTOCOL_NONE &&
           controlInterfaceNumber != 0xff &&
           dataInterfaceNumber != 0xff &&
           inEndpoint != 0 &&
           outEndpoint != 0;
  }
};

// Raw Ethernet frame delivered from / accepted by an opened USB network
// interface. `data` points at the bare Ethernet frame (dst/src MAC + ethertype
// + payload); the NCM NTB / ECM framing is added and stripped by the library.
struct EspUsbHostNetworkFrame
{
  uint8_t address = 0;
  EspUsbHostNetworkProtocol protocol = ESP_USB_HOST_NETWORK_PROTOCOL_NONE;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// lwIP (esp_netif) attach configuration for a USB network interface. The
// default is a DHCP client so the USB NIC (or the peer's DHCP server) hands the
// host an address. Set dhcpClient=false and fill ip/gateway/subnet for a static
// address.
struct EspUsbHostNetworkConfig
{
  bool dhcpClient = true;
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;
};

// Lightweight counters for diagnosing the USB network data path.
struct EspUsbHostNetworkStats
{
  bool ready = false;
  bool linkUp = false;
  bool netifAttached = false;
  uint32_t rxNtb = 0;    // NTBs received on bulk IN
  uint32_t rxFrames = 0; // Ethernet datagrams extracted from NTBs
  uint32_t txFrames = 0; // frames sent (NTB built + bulk OUT ok)
  uint32_t txFails = 0;  // frame send failures
  // NTBs dropped because wBlockLength exceeded ntbInSize. Non-zero means the
  // device ignored (or never offered) SET_NTB_INPUT_SIZE and is batching beyond
  // the negotiated maximum: every datagram in such an NTB is lost, which shows
  // up as heavy TCP retransmission rather than as a link failure.
  uint32_t rxOversized = 0;
  uint16_t ntbInSize = 0; // negotiated device->host NTB limit / receive buffer size
};

struct EspUsbHostVendorInterface
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t inEndpoint = 0;
  uint8_t outEndpoint = 0;
  uint16_t inMaxPacketSize = 0;
  uint16_t outMaxPacketSize = 0;
};

struct EspUsbHostVendorData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t endpoint = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// --- CCID (USB smart card reader, bInterfaceClass 0x0b) ---------------------

// Low two bits of bStatus in an RDR_to_PC response.
enum EspUsbHostCcidIccStatus : uint8_t
{
  ESP_USB_HOST_CCID_ICC_ACTIVE = 0,   // card present and activated
  ESP_USB_HOST_CCID_ICC_INACTIVE = 1, // card present, not activated
  ESP_USB_HOST_CCID_ICC_ABSENT = 2,   // no card in the slot
};

// Top two bits of bStatus in an RDR_to_PC response.
enum EspUsbHostCcidCommandStatus : uint8_t
{
  ESP_USB_HOST_CCID_COMMAND_OK = 0,
  ESP_USB_HOST_CCID_COMMAND_FAILED = 1,
  // Not a final response: the reader needs more time and will send another.
  ESP_USB_HOST_CCID_COMMAND_TIME_EXTENSION = 2,
};

// bPowerSelect of PC_to_RDR_IccPowerOn.
enum EspUsbHostCcidVoltage : uint8_t
{
  ESP_USB_HOST_CCID_VOLTAGE_AUTO = 0,
  ESP_USB_HOST_CCID_VOLTAGE_5V = 1,
  ESP_USB_HOST_CCID_VOLTAGE_3V = 2,
  ESP_USB_HOST_CCID_VOLTAGE_1V8 = 3,
};

// dwFeatures bits 16..18: how much of the ISO 7816 stack the reader implements.
enum EspUsbHostCcidExchangeLevel : uint8_t
{
  ESP_USB_HOST_CCID_EXCHANGE_CHARACTER = 0,
  ESP_USB_HOST_CCID_EXCHANGE_TPDU = 1,
  ESP_USB_HOST_CCID_EXCHANGE_SHORT_APDU = 2,
  ESP_USB_HOST_CCID_EXCHANGE_EXTENDED_APDU = 3,
};

struct EspUsbHostCcidInterface
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t inEndpoint = 0;        // bulk IN (RDR_to_PC)
  uint8_t outEndpoint = 0;       // bulk OUT (PC_to_RDR)
  uint8_t interruptEndpoint = 0; // interrupt IN (slot change), 0 when absent
  uint16_t inMaxPacketSize = 0;
  uint16_t outMaxPacketSize = 0;
  // Fields below come from the CCID class descriptor (bDescriptorType 0x21).
  // Readers that do not expose one keep the defaults.
  bool hasClassDescriptor = false;
  uint16_t bcdCCID = 0;
  uint8_t slotCount = 1;     // bMaxSlotIndex + 1
  uint8_t voltageSupport = 0;
  uint32_t protocols = 0;    // dwProtocols: bit0 = T=0, bit1 = T=1
  uint32_t features = 0;     // dwFeatures
  uint32_t maxMessageLength = 0;
  uint8_t maxBusySlots = 1;
  EspUsbHostCcidExchangeLevel exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_CHARACTER;
};

struct EspUsbHostCcidStatus
{
  uint8_t address = 0;
  uint8_t slot = 0;
  EspUsbHostCcidIccStatus iccStatus = ESP_USB_HOST_CCID_ICC_ABSENT;
  EspUsbHostCcidCommandStatus commandStatus = ESP_USB_HOST_CCID_COMMAND_OK;
  uint8_t error = 0; // bError, meaningful when commandStatus is FAILED
  bool present = false;
  bool active = false;
};

struct EspUsbHostCcidSlotEvent
{
  uint8_t address = 0;
  uint8_t slot = 0;
  bool present = false;
};

// Raw RDR_to_PC response, for ccidMessage(). data points into the library's
// per-device buffer and is only valid until the next CCID call.
struct EspUsbHostCcidResponse
{
  uint8_t messageType = 0;
  uint8_t slot = 0;
  uint8_t sequence = 0;
  uint8_t status = 0;         // raw bStatus
  uint8_t error = 0;          // bError
  uint8_t chainParameter = 0; // bChainParameter / bClockStatus / bRFU
  EspUsbHostCcidIccStatus iccStatus = ESP_USB_HOST_CCID_ICC_ABSENT;
  EspUsbHostCcidCommandStatus commandStatus = ESP_USB_HOST_CCID_COMMAND_OK;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// How vendorOpen() drives the bulk IN endpoint.
//
// Continuous keeps an IN transfer permanently outstanding and buffers whatever
// arrives, which suits a device that streams unprompted; vendorRead() then
// reads from that buffer. On-demand starts no transfer at all and leaves the
// endpoint idle until vendorReadSync() asks for data, which is what a
// transactional protocol needs -- a Bulk-Only Transport device answers only
// inside a transaction, and polling it outside one is a transfer error.
enum EspUsbHostVendorReadMode : uint8_t
{
  ESP_USB_HOST_VENDOR_READ_CONTINUOUS = 0,
  ESP_USB_HOST_VENDOR_READ_ON_DEMAND = 1,
};

// Default timeout for vendorReadSync().
static constexpr uint32_t ESP_USB_HOST_VENDOR_READ_DEFAULT_TIMEOUT_MS = 1000;

// Largest single continuous bulk IN transfer vendorOpen() will set up. One DMA
// buffer of this size is allocated per device, so the practical limit is memory
// rather than this constant.
static constexpr size_t ESP_USB_HOST_VENDOR_READ_MAX_TRANSFER_BYTES = 65536;

// Diagnostic snapshot of an asynchronous bulk OUT queue. Counters are updated
// from the caller task (submitted, queueFullEvents) and from the USB client task
// (completed, errors, bytes, zlp), so the snapshot is consistent per field but
// not necessarily taken at a single instant.
struct EspUsbHostWriteQueueStats
{
  uint32_t submitted = 0;       // transfers handed to the USB driver
  uint32_t completed = 0;       // completion callbacks received
  uint32_t errors = 0;          // completions with a status other than COMPLETED
  uint32_t queueFullEvents = 0; // acquires that had to wait for a free slot
  uint32_t zlp = 0;             // zero-length transfers sent
  uint64_t bytes = 0;           // bytes of completed transfers
};

// The vendor bulk OUT queue and the CDC serial OUT queue report the same shape.
using EspUsbHostVendorWriteStats = EspUsbHostWriteQueueStats;
using EspUsbHostSerialWriteStats = EspUsbHostWriteQueueStats;

// Diagnostic snapshot of the asynchronous bulk IN queue. Every field but
// `submitted` is written only from the USB client task, and `submitted` also
// counts the transfers vendorReadQueueBegin() hands over from the caller task,
// so the snapshot is consistent per field rather than taken at one instant.
//
// bytes / completed is the mean transfer the device actually filled, which is
// what says whether a stream is packet-bound or transfer-bound: a stream that
// keeps filling whole slots is limited by the bus, while one that returns short
// transfers is limited by the device.
struct EspUsbHostVendorReadStats
{
  uint32_t submitted = 0;        // transfers handed to the USB driver
  uint32_t completed = 0;        // completion callbacks received
  uint32_t errors = 0;           // completions with a status other than COMPLETED
  uint32_t shortTransfers = 0;   // completions that ended before filling the slot
  uint32_t resubmitFailures = 0; // slots that went idle because a resubmit failed
  uint32_t starved = 0;          // completions that found no other transfer in flight
  uint64_t bytes = 0;            // bytes delivered by completed transfers
};

struct EspUsbHostHIDReportDescriptor
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint16_t hidVersion = 0;
  uint8_t countryCode = 0;
  uint8_t descriptorType = USB_HID_REPORT_DESC;
  uint16_t reportedLength = 0;
  uint16_t length = 0;
  uint8_t data[ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE] = {};
};

struct EspUsbHostHIDReportData
{
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
  const uint8_t *rawData = nullptr;
  size_t rawLength = 0;
  const uint8_t *reportData = nullptr;
  size_t reportLength = 0;
};

struct EspUsbHostHIDFieldValue
{
  uint8_t reportId = 0;
  uint16_t usagePage = 0;
  uint16_t usage = 0;
  int32_t value = 0;
  int32_t logicalMin = 0;
  int32_t logicalMax = 0;
  uint16_t bitOffset = 0;
  uint8_t bitSize = 0;
  uint8_t flags = 0;
};

struct EspUsbHostKeyboardEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  bool pressed = false;
  bool released = false;
  uint8_t keycode = 0;
  uint8_t ascii = 0;
  uint16_t unicode = 0; // Unicode code point (0 if none); ascii is its Latin-1 low byte
  uint8_t modifiers = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
};

// A format-independent snapshot of the Keyboard/Keypad usage page. Boot and
// bitmap (NKRO) reports are both normalized to the same 256-bit key map.
struct EspUsbHostKeyboardState : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t bitmap[ESP_USB_HOST_KEYBOARD_BITMAP_SIZE] = {};
  uint8_t changedBitmap[ESP_USB_HOST_KEYBOARD_BITMAP_SIZE] = {};
  uint8_t modifiers = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;

  bool isDown(uint8_t keycode) const
  {
    return (bitmap[keycode >> 3] & static_cast<uint8_t>(1u << (keycode & 7))) != 0;
  }

  bool wasPressed(uint8_t keycode) const
  {
    return isDown(keycode) &&
           (changedBitmap[keycode >> 3] & static_cast<uint8_t>(1u << (keycode & 7))) != 0;
  }

  bool wasReleased(uint8_t keycode) const
  {
    return !isDown(keycode) &&
           (changedBitmap[keycode >> 3] & static_cast<uint8_t>(1u << (keycode & 7))) != 0;
  }
};

struct EspUsbHostMouseEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  // AC Pan (horizontal wheel / tilt). Always 0 for a boot mouse report, which
  // has no field for it.
  int16_t pan = 0;
  uint8_t buttons = 0;
  uint8_t previousButtons = 0;
  // All buttons the descriptor declares, bit 0 = button 1. `buttons` keeps the
  // low 8 for compatibility; a mouse with more than 8 buttons (gaming mice
  // routinely declare 16) reports the rest only here. buttonCount is 0 when the
  // report was decoded as a boot mouse, where the count is not declared.
  uint16_t buttonMask = 0;
  uint16_t previousButtonMask = 0;
  uint8_t buttonCount = 0;
  bool moved = false;
  bool buttonsChanged = false;
};

struct EspUsbHostHIDInput
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
  uint8_t subclass = 0;
  uint8_t protocol = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostSerialData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  // Which CDC port of the device this data came from, numbered from 0 in
  // descriptor order. Always 0 for a single-port device or a vendor VCP.
  uint8_t port = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

// One CDC-ACM (or vendor VCP) serial port of one device. Read with
// EspUsbHost::getSerialPortInfo() to map a port index onto the interface and
// endpoint numbers a device published, which is what lets a sketch tell two
// otherwise identical ACM functions apart.
struct EspUsbHostSerialPortInfo
{
  uint8_t address = 0;
  uint8_t port = 0;
  uint8_t controlInterfaceNumber = 0xff;
  uint8_t dataInterfaceNumber = 0xff;
  uint8_t inEndpointAddress = 0;
  uint8_t outEndpointAddress = 0;
  uint16_t outPacketSize = 0;
  // A CH340 / CP210x / FTDI / PL2303 bridge driven through its vendor protocol
  // rather than a standard CDC-ACM function. Such a bridge is always port 0 and
  // is the only port of its device.
  bool vendorSerial = false;
  // Both directions are claimed and the port can carry data.
  bool ready = false;
};

struct EspUsbHostMidiMessage
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t cable = 0;
  uint8_t codeIndex = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  const uint8_t *raw = nullptr;
  size_t length = 0;
};

// A cable number occupies 4 bits of the USB-MIDI 1.0 packet header, so an
// endpoint carries at most 16 of them.
static constexpr uint8_t ESP_USB_HOST_MIDI_MAX_CABLES = 16;

// Class-specific descriptor type (CS_ENDPOINT) and the MS_GENERAL subtype that
// the MIDI Streaming class document gives the descriptor following a bulk
// endpoint.
static constexpr uint8_t ESP_USB_HOST_MIDI_CS_ENDPOINT = 0x25;
static constexpr uint8_t ESP_USB_HOST_MIDI_MS_GENERAL = 0x01;

// Cable configuration of a device's MIDI Streaming interface, filled in from the
// descriptors at enumeration so the count is known before any traffic arrives.
//
// The two counts are directions as the host sees them: inCableCount is device to
// host, outCableCount is host to device. Beware that the class document names
// the jacks the other way round, see espUsbHostMidiEndpointCableCount().
struct EspUsbHostMidiPortInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t inCableCount = 0;
  uint8_t outCableCount = 0;
};

// Number of cables carried by a MIDI Streaming bulk endpoint, decoded from the
// class-specific endpoint descriptor that follows it:
//
//   bLength, CS_ENDPOINT, MS_GENERAL, bNumEmbMIDIJack, baAssocJackID[]
//
// A cable number is an index into baAssocJackID, so the cables of an endpoint are
// numbered 0 .. bNumEmbMIDIJack - 1, matching EspUsbHostMidiMessage::cable.
//
// The embedded jacks listed here are named from the device's point of view and
// are therefore the opposite of the endpoint direction: the descriptor on a bulk
// IN endpoint lists Embedded MIDI OUT Jacks (device to host) and the one on a
// bulk OUT endpoint lists Embedded MIDI IN Jacks. The direction that matters to a
// caller is the endpoint's, which is why this helper only returns the count and
// leaves the naming to the caller.
//
// Returns 0 for a descriptor that is not MS_GENERAL, declares more cables than a
// cable number can address, or is too short to hold the jack IDs it declares.
inline uint8_t espUsbHostMidiEndpointCableCount(const uint8_t *data)
{
  if (!data || data[0] < 4 ||
      data[1] != ESP_USB_HOST_MIDI_CS_ENDPOINT ||
      data[2] != ESP_USB_HOST_MIDI_MS_GENERAL)
  {
    return 0;
  }
  const uint8_t count = data[3];
  if (count > ESP_USB_HOST_MIDI_MAX_CABLES || data[0] < 4 + count)
  {
    return 0;
  }
  return count;
}

struct EspUsbHostAudioData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostAudioOutputRequest
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t endpointAddress = 0;
  uint32_t sampleRate = 0;
  uint8_t channels = 0;
  uint8_t bytesPerSample = 0;
  uint8_t bitsPerSample = 0;
  uint8_t *data = nullptr;
  size_t frameCount = 0;
  size_t byteCount = 0;
  size_t writtenFrames = 0;
};

struct EspUsbHostMscInquiry
{
  uint8_t peripheralDeviceType = 0;
  bool removable = false;
  char vendor[9] = {};
  char product[17] = {};
  char revision[5] = {};
};

struct EspUsbHostMscSense
{
  uint8_t responseCode = 0;
  uint8_t senseKey = 0;
  uint8_t additionalSenseCode = 0;
  uint8_t additionalSenseQualifier = 0;
};

struct EspUsbHostMscBlockDeviceInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t lun = 0;
  uint8_t maxLun = 0;
  uint64_t blockCount = 0;
  uint32_t blockSize = 0;
  uint64_t capacityBytes = 0;
};

// bInterfaceProtocol of an Audio Class interface: 0x00 for UAC1 (ADC 1.0) and
// 0x20 (IP_VERSION_02_00) for UAC2. Streams and Feature Units carry the value so
// callers can tell which descriptor and control model the device follows.
static constexpr uint8_t ESP_USB_HOST_AUDIO_PROTOCOL_UAC1 = 0x00;
static constexpr uint8_t ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 = 0x20;

struct EspUsbHostAudioStreamInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t alternate = 0;
  uint8_t endpointAddress = 0;
  bool input = false;
  bool output = false;
  uint8_t channels = 0;
  uint8_t bytesPerSample = 0;
  uint8_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint8_t sampleRateCount = 0;
  uint32_t sampleRates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
  uint32_t sampleRateMin = 0;
  uint32_t sampleRateMax = 0;
  uint32_t sampleRateResolution = 0;
  uint16_t maxPacketSize = 0;
  uint8_t interval = 0;
  // False when the stream's alternate setting was discovered but its endpoints
  // were not claimed, so it describes a format the device offers that this host
  // cannot currently start. Only one alternate setting per interface is claimed
  // during enumeration, so a device that splits formats across alternates (a
  // 16-bit and a 24-bit alternate, for example) reports the others this way.
  // Defaults to true so a hand-built stream array still selects normally.
  bool startable = true;
  uint8_t protocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
  // UAC2 only: the Audio Streaming interface's bTerminalLink and the Clock Source
  // entity reached through that terminal. UAC2 keeps sample rates in the clock
  // entity instead of the format descriptor, so sampleRates[] above is filled from
  // a class request against clockSourceId rather than from the descriptors.
  uint8_t terminalLink = 0;
  uint8_t clockSourceId = 0;
};

struct EspUsbHostAudioFeatureUnitInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t unitId = 0;
  uint8_t sourceId = 0;
  uint8_t channelCount = 0;
  // Bytes per bmaControls entry: taken from bControlSize on UAC1, fixed at 4 on
  // UAC2. UAC1 stores one bit per control, UAC2 two bits (01 = read-only,
  // 11 = host-programmable), so decode the masks below with
  // espUsbHostAudioFeatureHasControl() rather than by shifting directly.
  uint8_t controlSize = 0;
  uint32_t masterControls = 0;
  uint32_t channelControls[ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS] = {};
  uint8_t protocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
};

struct EspUsbHostAudioVolumeRange
{
  int16_t min = 0;
  int16_t max = 0;
  int16_t resolution = 0;
};

struct EspUsbHostAudioStreamSelection
{
  int index = -1;
  uint32_t sampleRate = 0;
  int score = 0;

  explicit operator bool() const
  {
    return index >= 0 && sampleRate > 0;
  }
};

using EspUsbHostAudioStreamFilter = bool (*)(uint32_t sampleRate,
                                             uint8_t channels,
                                             uint8_t bitsPerSample);

// Where a Feature Unit descriptor keeps its bmaControls array, and how many
// channels it describes. valid is false when the descriptor is too short to hold
// even the master control entry.
struct EspUsbHostAudioFeatureUnitLayout
{
  bool valid = false;
  uint8_t controlSize = 0;
  uint8_t controlOffset = 0;
  uint8_t channelCount = 0;
};

// UAC1 announces the bmaControls stride in bControlSize and starts the array at
// offset 6, leaving 7 bytes that are not controls (6 header + iFeature). UAC2
// dropped bControlSize for a fixed 4-byte stride, so its array starts at offset 5
// and only 6 bytes are not controls.
inline EspUsbHostAudioFeatureUnitLayout espUsbHostAudioFeatureUnitLayout(const uint8_t *data, uint8_t protocol)
{
  EspUsbHostAudioFeatureUnitLayout layout;
  if (!data || data[0] < 7)
  {
    return layout;
  }
  const bool uac2 = protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2;
  const uint8_t controlSize = uac2 ? 4 : data[5];
  const uint8_t fixedBytes = uac2 ? 6 : 7;
  if (controlSize == 0 || controlSize > 4 || data[0] < fixedBytes + controlSize)
  {
    return layout;
  }
  layout.valid = true;
  layout.controlSize = controlSize;
  layout.controlOffset = uac2 ? 5 : 6;
  layout.channelCount = static_cast<uint8_t>(((data[0] - fixedBytes) / controlSize) - 1);
  return layout;
}

// Feature Unit bmaControls decoding. UAC1 packs one bit per control (D0 Mute,
// D1 Volume, ...), UAC2 two bits per control where 01 means present but
// read-only and 11 means host-programmable. Both index the field by
// controlSelector - 1 (FU_MUTE = 1, FU_VOLUME = 2).
inline bool espUsbHostAudioFeatureHasControl(uint32_t controls,
                                             uint8_t controlSelector,
                                             uint8_t protocol)
{
  if (controlSelector == 0)
  {
    return false;
  }
  const uint8_t index = static_cast<uint8_t>(controlSelector - 1);
  if (protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    return index < 16 && ((controls >> (index * 2)) & 0x03) != 0;
  }
  return index < 32 && ((controls >> index) & 0x01) != 0;
}

// True when the control can be written. UAC1 has no read-only encoding, so a
// declared control counts as writable there.
inline bool espUsbHostAudioFeatureControlWritable(uint32_t controls,
                                                  uint8_t controlSelector,
                                                  uint8_t protocol)
{
  if (protocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    return espUsbHostAudioFeatureHasControl(controls, controlSelector, protocol);
  }
  if (controlSelector == 0)
  {
    return false;
  }
  const uint8_t index = static_cast<uint8_t>(controlSelector - 1);
  return index < 16 && ((controls >> (index * 2)) & 0x03) == 0x03;
}

// Isochronous endpoint usage type (bmAttributes D5..D4): 00 data, 01 feedback,
// 10 implicit feedback data. A UAC2 asynchronous playback interface adds a
// feedback IN endpoint beside the data OUT endpoint; it carries a rate estimate
// in 16.16 (high speed) or 10.14 (full speed) format, not audio, so it must not
// be mistaken for a capture stream.
inline bool espUsbHostAudioIsFeedbackEndpoint(uint8_t bmAttributes)
{
  return (bmAttributes & 0x03) == 0x01 && (bmAttributes & 0x30) == 0x10;
}

inline uint32_t espUsbHostAudioReadU32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

// Explicit feedback payload (USB 2.0 section 5.12.4.2) normalised to 16.16
// samples per (micro)frame, which is what the endpoint reports: samples per
// 1 ms frame at full speed and per 125 us microframe at high speed. A 3-byte
// payload holds the value in 10.14 format and is shifted up by two; a 4-byte
// payload already is 16.16. Returns 0 when the payload is unusable, matching how
// a zeroed or short packet must be ignored rather than applied.
inline uint32_t espUsbHostAudioDecodeFeedbackQ16(const uint8_t *data, size_t length)
{
  if (!data || length < 3)
  {
    return 0;
  }
  if (length == 3)
  {
    const uint32_t value = static_cast<uint32_t>(data[0]) |
                           (static_cast<uint32_t>(data[1]) << 8) |
                           (static_cast<uint32_t>(data[2]) << 16);
    return value << 2;
  }
  return espUsbHostAudioReadU32(data);
}

// Sample rate in Hz carried by a decoded feedback value. Full speed reports
// samples per 1 ms frame, high speed samples per 125 us microframe.
inline uint32_t espUsbHostAudioFeedbackSampleRate(uint32_t feedbackQ16, bool highSpeed)
{
  const uint64_t framesPerSecond = highSpeed ? 8000u : 1000u;
  return static_cast<uint32_t>((static_cast<uint64_t>(feedbackQ16) * framesPerSecond) >> 16);
}

// A feedback value far from the negotiated rate is a device or bus glitch, not a
// rate the host should follow. The +/-12.5% window is the one Linux's
// snd_usb_audio applies before it accepts a feedback update.
inline bool espUsbHostAudioFeedbackRatePlausible(uint32_t rateHz, uint32_t nominalHz)
{
  if (rateHz == 0 || nominalHz == 0)
  {
    return false;
  }
  const uint32_t margin = nominalHz / 8;
  return rateHz >= nominalHz - margin && rateHz <= nominalHz + margin;
}

// wNumSubRanges as declared by a UAC2 RANGE response, ignoring whether the
// payload actually carries that many subranges. Used to size the follow-up
// request after a 2-byte probe.
inline size_t espUsbHostAudioRangeDeclaredCount(const uint8_t *data, size_t length)
{
  if (!data || length < 2)
  {
    return 0;
  }
  return static_cast<size_t>(data[0]) | (static_cast<size_t>(data[1]) << 8);
}

// Subranges that are both declared and completely present in the payload, so a
// response truncated by a short wLength still yields the entries it did return.
// subRangeSize is 12 for the 4-byte sample frequency control and 6 for the
// 2-byte volume control.
inline size_t espUsbHostAudioRangeSubRangeCount(const uint8_t *data, size_t length, size_t subRangeSize)
{
  if (subRangeSize == 0 || length < 2 + subRangeSize)
  {
    return 0;
  }
  const size_t declared = espUsbHostAudioRangeDeclaredCount(data, length);
  const size_t available = (length - 2) / subRangeSize;
  return declared < available ? declared : available;
}

// Flatten a UAC2 SAM_FREQ_CONTROL RANGE response into discrete rates. Each
// subrange is MIN/MAX/RES as 4-byte values; a discrete rate is encoded as
// MIN == MAX, while a continuous subrange contributes its endpoints (and the
// steps in between when RES divides the span and there is room).
inline size_t espUsbHostAudioDecodeSampleRateRange(const uint8_t *data,
                                                   size_t length,
                                                   uint32_t *rates,
                                                   size_t maxRates)
{
  const size_t subRanges = espUsbHostAudioRangeSubRangeCount(data, length, 12);
  if (!rates || maxRates == 0 || subRanges == 0)
  {
    return 0;
  }

  size_t count = 0;
  auto add = [&](uint32_t rate)
  {
    if (rate == 0 || count >= maxRates)
    {
      return;
    }
    for (size_t i = 0; i < count; i++)
    {
      if (rates[i] == rate)
      {
        return;
      }
    }
    rates[count++] = rate;
  };

  for (size_t i = 0; i < subRanges && count < maxRates; i++)
  {
    const uint8_t *entry = &data[2 + i * 12];
    const uint32_t min = espUsbHostAudioReadU32(entry);
    const uint32_t max = espUsbHostAudioReadU32(entry + 4);
    const uint32_t resolution = espUsbHostAudioReadU32(entry + 8);
    add(min);
    if (max == min)
    {
      continue;
    }
    if (resolution > 0 && max > min)
    {
      for (uint32_t rate = min + resolution; rate < max && count < maxRates; rate += resolution)
      {
        add(rate);
      }
    }
    add(max);
  }
  return count;
}

// Decode the first subrange of a UAC2 VOLUME_CONTROL RANGE response. Volume is
// a signed 16-bit value in 1/256 dB units, same as UAC1.
inline bool espUsbHostAudioDecodeVolumeRange(const uint8_t *data,
                                             size_t length,
                                             EspUsbHostAudioVolumeRange &range)
{
  if (espUsbHostAudioRangeSubRangeCount(data, length, 6) == 0)
  {
    return false;
  }
  auto readI16 = [](const uint8_t *value) -> int16_t
  {
    return static_cast<int16_t>(static_cast<uint16_t>(value[0]) |
                                (static_cast<uint16_t>(value[1]) << 8));
  };
  range.min = readI16(&data[2]);
  range.max = readI16(&data[4]);
  range.resolution = readI16(&data[6]);
  return true;
}

inline bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream, uint32_t sampleRate)
{
  if (sampleRate == 0)
  {
    return false;
  }

  if (stream.sampleRateCount > 0)
  {
    for (uint8_t i = 0; i < stream.sampleRateCount && i < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++)
    {
      if (stream.sampleRates[i] == sampleRate)
      {
        return true;
      }
    }
    return false;
  }

  if (stream.sampleRateMin > 0 && stream.sampleRateMax >= stream.sampleRateMin)
  {
    if (sampleRate < stream.sampleRateMin || sampleRate > stream.sampleRateMax)
    {
      return false;
    }
    if (stream.sampleRateResolution == 0)
    {
      return true;
    }
    return ((sampleRate - stream.sampleRateMin) % stream.sampleRateResolution) == 0;
  }

  return stream.sampleRate == 0 || stream.sampleRate == sampleRate;
}

inline uint32_t espUsbHostAudioStreamPreferredSampleRate(const EspUsbHostAudioStreamInfo &stream, uint32_t preferredSampleRate)
{
  if (espUsbHostAudioStreamSupportsSampleRate(stream, preferredSampleRate))
  {
    return preferredSampleRate;
  }

  if (stream.sampleRate > 0 && espUsbHostAudioStreamSupportsSampleRate(stream, stream.sampleRate))
  {
    return stream.sampleRate;
  }

  if (stream.sampleRateCount > 0)
  {
    return stream.sampleRates[0];
  }

  if (stream.sampleRateMin > 0)
  {
    return stream.sampleRateMin;
  }

  return 0;
}

inline bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                            uint8_t channels,
                                            uint8_t bytesPerSample,
                                            uint8_t bitsPerSample,
                                            uint32_t sampleRate)
{
  return stream.channels == channels &&
         stream.bytesPerSample == bytesPerSample &&
         stream.bitsPerSample == bitsPerSample &&
         espUsbHostAudioStreamSupportsSampleRate(stream, sampleRate);
}

inline bool espUsbHostAudioStreamCandidateRateExists(const uint32_t *rates, size_t count, uint32_t rate)
{
  for (size_t i = 0; i < count; i++)
  {
    if (rates[i] == rate)
    {
      return true;
    }
  }
  return false;
}

inline size_t espUsbHostAudioStreamCandidateSampleRates(const EspUsbHostAudioStreamInfo &stream,
                                                        uint32_t *rates,
                                                        size_t maxRates)
{
  if (!rates || maxRates == 0)
  {
    return 0;
  }

  size_t count = 0;
  auto addRate = [&](uint32_t rate)
  {
    if (rate == 0 ||
        !espUsbHostAudioStreamSupportsSampleRate(stream, rate) ||
        espUsbHostAudioStreamCandidateRateExists(rates, count, rate) ||
        count >= maxRates)
    {
      return;
    }
    rates[count++] = rate;
  };

  addRate(48000);
  addRate(44100);
  addRate(stream.sampleRate);
  for (uint8_t i = 0; i < stream.sampleRateCount && i < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++)
  {
    addRate(stream.sampleRates[i]);
  }
  if (stream.sampleRateMax > 0)
  {
    addRate(stream.sampleRateMax);
  }
  addRate(stream.sampleRateMin);

  return count;
}

inline int espUsbHostAudioStreamScore(const EspUsbHostAudioStreamInfo &stream, uint32_t sampleRate)
{
  int score = 0;

  if (sampleRate == 48000)
  {
    score += 10000;
  }
  else if (sampleRate == 44100)
  {
    score += 9000;
  }
  else if (sampleRate >= 32000)
  {
    score += 6000 + static_cast<int>(sampleRate / 1000);
  }
  else
  {
    score += static_cast<int>(sampleRate / 100);
  }

  if (stream.bitsPerSample == 16)
  {
    score += 1000;
    if (stream.bytesPerSample == 2)
    {
      score += 100;
    }
  }
  else if (stream.bitsPerSample == 24)
  {
    score += 800;
    if (stream.bytesPerSample == 3 || stream.bytesPerSample == 4)
    {
      score += 50;
    }
  }
  else if (stream.bitsPerSample == 32)
  {
    score += 700;
    if (stream.bytesPerSample == 4)
    {
      score += 50;
    }
  }
  else if (stream.bitsPerSample == 8)
  {
    score += 200;
    if (stream.bytesPerSample == 1)
    {
      score += 25;
    }
  }
  else
  {
    score += stream.bitsPerSample;
  }

  if (stream.channels == 2)
  {
    score += 300;
  }
  else if (stream.channels == 1)
  {
    score += 200;
  }
  else
  {
    score += stream.channels;
  }

  return score;
}

inline uint32_t espUsbHostAudioStreamBestSampleRate(const EspUsbHostAudioStreamInfo &stream)
{
  uint32_t rates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES + 4] = {};
  const size_t count = espUsbHostAudioStreamCandidateSampleRates(stream, rates, sizeof(rates) / sizeof(rates[0]));
  if (count == 0)
  {
    return 0;
  }

  uint32_t bestRate = 0;
  int bestScore = -1;
  for (size_t i = 0; i < count; i++)
  {
    const int score = espUsbHostAudioStreamScore(stream, rates[i]);
    if (bestScore < 0 || score > bestScore)
    {
      bestRate = rates[i];
      bestScore = score;
    }
  }
  return bestRate;
}

inline EspUsbHostAudioStreamSelection espUsbHostSelectAudioStream(const EspUsbHostAudioStreamInfo *streams,
                                                                  size_t count,
                                                                  bool input,
                                                                  EspUsbHostAudioStreamFilter filter = nullptr)
{
  EspUsbHostAudioStreamSelection best;
  if (!streams)
  {
    return best;
  }

  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostAudioStreamInfo &stream = streams[i];
    if (input ? !stream.input : !stream.output)
    {
      continue;
    }
    if (!stream.startable)
    {
      continue;
    }

    uint32_t rates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES + 4] = {};
    const size_t rateCount = espUsbHostAudioStreamCandidateSampleRates(stream, rates, sizeof(rates) / sizeof(rates[0]));
    for (size_t rateIndex = 0; rateIndex < rateCount; rateIndex++)
    {
      const uint32_t sampleRate = rates[rateIndex];
      if (filter && !filter(sampleRate, stream.channels, stream.bitsPerSample))
      {
        continue;
      }

      const int score = espUsbHostAudioStreamScore(stream, sampleRate);
      if (best.index < 0 || score > best.score)
      {
        best.index = static_cast<int>(i);
        best.sampleRate = sampleRate;
        best.score = score;
      }
    }
  }
  return best;
}

// Pick the best stream that satisfies a partially specified PCM format. A zero
// means "no preference", so (0, 0, 0) is "whatever this device does best" and
// (2, 0, 48000) is "48 kHz stereo, any sample width". Fully specified arguments
// behave like an exact-match lookup, except that several equally matching
// alternates are ranked by espUsbHostAudioStreamScore() instead of resolving to
// whichever came first in the descriptors.
inline EspUsbHostAudioStreamSelection espUsbHostSelectAudioStreamForFormat(const EspUsbHostAudioStreamInfo *streams,
                                                                          size_t count,
                                                                          bool input,
                                                                          uint8_t channels,
                                                                          uint8_t bitsPerSample,
                                                                          uint32_t sampleRate)
{
  EspUsbHostAudioStreamSelection best;
  if (!streams)
  {
    return best;
  }

  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostAudioStreamInfo &stream = streams[i];
    if ((input ? !stream.input : !stream.output) || !stream.startable)
    {
      continue;
    }
    if ((channels != 0 && stream.channels != channels) ||
        (bitsPerSample != 0 && stream.bitsPerSample != bitsPerSample))
    {
      continue;
    }

    // A requested rate must be supported as-is. Without one, rank every rate the
    // stream offers, including the standard rates a continuous range covers.
    uint32_t rates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES + 4] = {};
    size_t rateCount = 0;
    if (sampleRate != 0)
    {
      if (!espUsbHostAudioStreamSupportsSampleRate(stream, sampleRate))
      {
        continue;
      }
      rates[rateCount++] = sampleRate;
    }
    else
    {
      rateCount = espUsbHostAudioStreamCandidateSampleRates(stream, rates, sizeof(rates) / sizeof(rates[0]));
    }

    for (size_t rateIndex = 0; rateIndex < rateCount; rateIndex++)
    {
      const int score = espUsbHostAudioStreamScore(stream, rates[rateIndex]);
      if (best.index < 0 || score > best.score)
      {
        best.index = static_cast<int>(i);
        best.sampleRate = rates[rateIndex];
        best.score = score;
      }
    }
  }
  return best;
}

inline EspUsbHostAudioStreamSelection espUsbHostSelectAudioInputStream(const EspUsbHostAudioStreamInfo *streams,
                                                                       size_t count,
                                                                       EspUsbHostAudioStreamFilter filter = nullptr)
{
  return espUsbHostSelectAudioStream(streams, count, true, filter);
}

inline EspUsbHostAudioStreamSelection espUsbHostSelectAudioOutputStream(const EspUsbHostAudioStreamInfo *streams,
                                                                        size_t count,
                                                                        EspUsbHostAudioStreamFilter filter = nullptr)
{
  return espUsbHostSelectAudioStream(streams, count, false, filter);
}

// ---------------------------------------------------------------------------
// USB Video Class (UVC)
// ---------------------------------------------------------------------------
//
// UVC describes what a camera can send as a two-level tree: a Format descriptor
// (what the pixels mean) with one or more Frame descriptors beneath it (what
// size, at which rates). This host flattens that tree into one
// EspUsbHostVideoStreamInfo per format/frame pair, because a caller picks both
// together and the Probe/Commit negotiation names both together.

// Pixel formats this host names. UNKNOWN doubles as the "any format" wildcard in
// espUsbHostSelectVideoStream(), which is why it is zero. UNCOMPRESSED is the
// catch-all for a Format Uncompressed descriptor whose GUID is not one of the
// FourCCs below: the frame sizes and rates are still usable, only the byte
// layout is unnamed.
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN = 0;
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_MJPEG = 1;
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_YUY2 = 2;
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_NV12 = 3;
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_H264 = 4;
static constexpr uint8_t ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED = 5;

// Length of the Video Probe/Commit Control payload by class revision. A camera
// answers GET_LEN with the one it implements, and a UVC 1.0 camera stalls a
// 34-byte SET_CUR, so the negotiated length has to be carried rather than
// assumed. MAX_LENGTH covers UVC 1.5 so a buffer sized by it always fits the
// reply, even though this host never writes the 1.5-only fields.
static constexpr size_t ESP_USB_HOST_VIDEO_PROBE_LENGTH_10 = 26;
static constexpr size_t ESP_USB_HOST_VIDEO_PROBE_LENGTH_11 = 34;
static constexpr size_t ESP_USB_HOST_VIDEO_PROBE_MAX_LENGTH = 48;

struct EspUsbHostVideoStreamInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t alternate = 0;
  uint8_t endpointAddress = 0;
  // False for a bulk-only streaming interface. Isochronous is the common case;
  // bulk cameras exist and stream over a single alternate setting.
  bool isochronous = true;
  uint8_t format = ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN;
  // Indices as the device numbers them, which is what Probe/Commit takes. Both
  // are 1-based in UVC, so zero means "not decoded".
  uint8_t formatIndex = 0;
  uint8_t frameIndex = 0;
  // bNumFrameDescriptors and bDefaultFrameIndex of the parent Format, repeated
  // on every stream that came from it.
  uint8_t frameCount = 0;
  uint8_t defaultFrameIndex = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  // Uncompressed formats only; MJPEG leaves it zero.
  uint8_t bitsPerPixel = 0;
  // All intervals are in 100 ns units, the unit UVC uses throughout. Use
  // espUsbHostVideoFrameIntervalToFps() rather than dividing at the call site.
  // frameInterval is dwDefaultFrameInterval: the rate to use when the caller
  // expresses no preference.
  uint32_t frameInterval = 0;
  // A frame descriptor is either discrete (frameIntervalCount entries in
  // frameIntervals) or continuous (min/max/step, with frameIntervalCount zero).
  // Never both.
  uint8_t frameIntervalCount = 0;
  uint32_t frameIntervals[ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS] = {};
  uint32_t frameIntervalMin = 0;
  uint32_t frameIntervalMax = 0;
  uint32_t frameIntervalStep = 0;
  // dwMaxVideoFrameBufferSize: the largest single video frame this format/frame
  // pair can produce. A caller sizing a frame buffer must use this rather than
  // width * height * bytes, which is wrong for MJPEG.
  uint32_t maxVideoFrameBufferSize = 0;
  // Bytes per microframe the streaming endpoint's alternate setting offers,
  // already multiplied out by espUsbHostVideoIsocPayloadSize().
  uint32_t maxPayloadSize = 0;
  uint8_t interval = 0;
  // False when the camera advertises the format but this host has no way to run
  // it: the VideoStreaming interface offers no usable alternate setting, or it
  // was not claimed. Unlike audio, which binds a format to one alternate, a UVC
  // camera describes every format on alternate 0 and picks the alternate at
  // start time from the negotiated payload size, so this is a property of the
  // interface rather than of the individual format. Defaults to true so a
  // hand-built array still selects.
  bool startable = true;
};

// One assembled video frame, handed to the onVideoFrame() callback.
//
// data points into a buffer the library owns and reuses for the next frame, so a
// sketch that needs to keep the image must copy it before returning. The callback
// runs on the USB client task, which is also the task that resubmits the
// streaming transfer, so anything slow in it costs isochronous packets that
// cannot be retried -- see docs/usb-host-advanced.md.
struct EspUsbHostVideoFrame
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t format = ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN;
  // Counts frames delivered since videoStart(), complete and incomplete alike, so
  // a gap in it is a frame that was dropped outright.
  uint32_t sequence = 0;
  // False when the camera flagged a payload bad, the frame overran the buffer, or
  // the end-of-frame payload never arrived and the frame ID toggled instead. The
  // bytes up to that point are still handed over: a partial MJPEG frame is often
  // displayable, and dropping it silently would hide a link that is losing
  // packets. A sketch that cannot use one should check this and return.
  bool complete = true;
  bool hasPresentationTime = false;
  uint32_t presentationTime = 0;
};

// Counters kept while streaming, reset by videoStart(). Isochronous transfers are
// not retried, so these are the only way to tell a healthy stream from one that
// is losing packets: on a good link everything but frames and payloads stays at
// zero.
struct EspUsbHostVideoStats
{
  uint32_t frames = 0;
  uint32_t framesIncomplete = 0;
  uint32_t payloads = 0;
  // Packets whose payload header would not decode. A camera does not send these;
  // a nonzero count means packets are arriving damaged.
  uint32_t headerErrors = 0;
  // Payloads the camera itself flagged bad with the error bit.
  uint32_t payloadErrors = 0;
  // Isochronous packets the host controller reported as failed. These are never
  // retried, so a nonzero count is lost image data; a count that climbs in step
  // with the transfers means the pipe is not carrying the stream at all.
  uint32_t packetErrors = 0;
  // Frames that ran past the frame buffer. The buffer is sized from the format's
  // dwMaxVideoFrameBufferSize, so this should not happen; when it does, the
  // camera is sending more than it declared.
  uint32_t overflows = 0;
  uint64_t bytes = 0;
};

struct EspUsbHostVideoStreamSelection
{
  int index = -1;
  uint32_t frameInterval = 0;
  int score = 0;

  explicit operator bool() const
  {
    return index >= 0 && frameInterval > 0;
  }
};

// One payload header, which prefixes every non-empty packet the streaming
// endpoint delivers. payloadOffset is where the image bytes start.
struct EspUsbHostVideoPayloadHeader
{
  uint8_t headerLength = 0;
  uint8_t info = 0;
  // Toggles on every frame boundary. Two consecutive payloads with different
  // frameId belong to different video frames even if neither carried endOfFrame,
  // which is how a dropped end-of-frame packet is recovered from.
  bool frameId = false;
  bool endOfFrame = false;
  bool stillImage = false;
  // The camera is telling the host this payload is bad. The frame it belongs to
  // has to be discarded; the bytes are not image data.
  bool error = false;
  bool endOfHeader = false;
  bool hasPresentationTime = false;
  uint32_t presentationTime = 0;
  bool hasSourceClock = false;
  uint32_t sourceClock = 0;
  uint16_t sourceClockCounter = 0;
  size_t payloadOffset = 0;
};

// The Video Probe and Commit Control payload, in host-native fields. Both
// controls carry the same structure; Probe negotiates and Commit applies.
struct EspUsbHostVideoProbeControl
{
  // bmHint bit 0 = dwFrameInterval is fixed, which is what a host asking for a
  // specific rate means.
  uint16_t hint = 0x0001;
  uint8_t formatIndex = 0;
  uint8_t frameIndex = 0;
  uint32_t frameInterval = 0;
  uint16_t keyFrameRate = 0;
  uint16_t pFrameRate = 0;
  uint16_t compQuality = 0;
  uint16_t compWindowSize = 0;
  uint16_t delay = 0;
  uint32_t maxVideoFrameSize = 0;
  // What the camera will put in one payload transfer. This is the number that
  // decides which alternate setting the host must select: an alternate whose
  // espUsbHostVideoIsocPayloadSize() is smaller cannot carry it.
  uint32_t maxPayloadTransferSize = 0;
  // UVC 1.1 and later only; zero on a 26-byte reply.
  uint32_t clockFrequency = 0;
  uint8_t framingInfo = 0;
  uint8_t preferredVersion = 0;
  uint8_t minVersion = 0;
  uint8_t maxVersion = 0;
};

inline uint16_t espUsbHostVideoReadU16(const uint8_t *data)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                               static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8));
}

inline uint32_t espUsbHostVideoReadU32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

inline void espUsbHostVideoWriteU16(uint8_t *data, uint16_t value)
{
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>(value >> 8);
}

inline void espUsbHostVideoWriteU32(uint8_t *data, uint32_t value)
{
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

// A UVC format GUID is {FourCC}-0000-0010-8000-00AA00389B71: the first four
// bytes are the FourCC and the remaining twelve are a fixed suffix. A GUID that
// does not carry that suffix is a vendor format, not a FourCC, so it is reported
// as UNCOMPRESSED rather than being decoded as four ASCII characters that happen
// to spell something.
inline uint8_t espUsbHostVideoFormatFromGuid(const uint8_t *guid)
{
  if (!guid)
  {
    return ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN;
  }
  static const uint8_t suffix[12] = {
      0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
  for (size_t i = 0; i < sizeof(suffix); i++)
  {
    if (guid[4 + i] != suffix[i])
    {
      return ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED;
    }
  }
  if (guid[0] == 'Y' && guid[1] == 'U' && guid[2] == 'Y' && guid[3] == '2')
  {
    return ESP_USB_HOST_VIDEO_FORMAT_YUY2;
  }
  if (guid[0] == 'N' && guid[1] == 'V' && guid[2] == '1' && guid[3] == '2')
  {
    return ESP_USB_HOST_VIDEO_FORMAT_NV12;
  }
  if (guid[0] == 'H' && guid[1] == '2' && guid[2] == '6' && guid[3] == '4')
  {
    return ESP_USB_HOST_VIDEO_FORMAT_H264;
  }
  return ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED;
}

inline const char *espUsbHostVideoFormatName(uint8_t format)
{
  switch (format)
  {
  case ESP_USB_HOST_VIDEO_FORMAT_MJPEG:
    return "MJPEG";
  case ESP_USB_HOST_VIDEO_FORMAT_YUY2:
    return "YUY2";
  case ESP_USB_HOST_VIDEO_FORMAT_NV12:
    return "NV12";
  case ESP_USB_HOST_VIDEO_FORMAT_H264:
    return "H.264";
  case ESP_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED:
    return "Uncompressed";
  default:
    return "Unknown";
  }
}

// Frame intervals are in 100 ns units, so a rate is 10000000 / interval. The
// division is rounded rather than truncated: 24 fps is advertised as 416667,
// and truncating that gives 23.
inline uint32_t espUsbHostVideoFrameIntervalToFps(uint32_t frameInterval)
{
  if (frameInterval == 0)
  {
    return 0;
  }
  return (10000000u + frameInterval / 2) / frameInterval;
}

inline uint32_t espUsbHostVideoFpsToFrameInterval(uint32_t fps)
{
  if (fps == 0)
  {
    return 0;
  }
  return 10000000u / fps;
}

// Bytes an isochronous endpoint can move per microframe. Bits 10:0 of
// wMaxPacketSize are the packet size and bits 12:11 are the number of
// *additional* transactions, which is how a high-speed UVC alternate offers more
// than 1024 bytes. Reading wMaxPacketSize directly understates such an alternate
// by up to 3x, which then picks an alternate too small for the camera's
// dwMaxPayloadTransferSize and produces a stream that never completes a frame.
// Transactions per service interval an isochronous endpoint asks for: 1, 2 or 3.
// Bits 12:11 of wMaxPacketSize hold one less than that, and 11b is reserved.
inline uint8_t espUsbHostVideoIsocTransactions(uint16_t wMaxPacketSize)
{
  const uint8_t additional = static_cast<uint8_t>((wMaxPacketSize >> 11) & 0x03u);
  // 11b is reserved. Claiming four transactions for it would reserve bandwidth
  // the device never offered, so it is read as a single transaction.
  return additional > 2 ? 1u : static_cast<uint8_t>(additional + 1u);
}

// Bytes an isochronous endpoint can move per service interval, as the descriptor
// declares it. This is what the device offers; what this host can take is
// VideoAlternateState::usablePayload, which differs when the controller cannot
// run more than one transaction per interval.
inline uint32_t espUsbHostVideoIsocPayloadSize(uint16_t wMaxPacketSize)
{
  const uint32_t size = wMaxPacketSize & 0x07FFu;
  const uint32_t additional = (wMaxPacketSize >> 11) & 0x03u;
  if (additional > 2)
  {
    // 11b is reserved. Claiming four transactions for it would reserve bandwidth
    // the device never offered, so fall back to a single transaction.
    return size;
  }
  return size * (additional + 1);
}

// Decodes a VS_FORMAT_UNCOMPRESSED (0x04) or VS_FORMAT_MJPEG (0x06) class
// descriptor into the format half of a stream. Frame-based formats (0x10/0x11,
// used by H.264 cameras) carry a different frame layout and are not decoded yet.
inline bool espUsbHostVideoDecodeFormatDescriptor(const uint8_t *data,
                                                  EspUsbHostVideoStreamInfo &stream)
{
  if (!data || data[0] < 3 || data[1] != 0x24)
  {
    return false;
  }
  if (data[2] == 0x06)
  {
    // bLength, CS_INTERFACE, VS_FORMAT_MJPEG, bFormatIndex,
    // bNumFrameDescriptors, bmFlags, bDefaultFrameIndex, ...
    if (data[0] < 11)
    {
      return false;
    }
    stream.format = ESP_USB_HOST_VIDEO_FORMAT_MJPEG;
    stream.formatIndex = data[3];
    stream.frameCount = data[4];
    stream.defaultFrameIndex = data[6];
    stream.bitsPerPixel = 0;
    return true;
  }
  if (data[2] == 0x04)
  {
    // ... bFormatIndex, bNumFrameDescriptors, guidFormat[16] at offset 5,
    // bBitsPerPixel, bDefaultFrameIndex, ...
    if (data[0] < 23)
    {
      return false;
    }
    stream.format = espUsbHostVideoFormatFromGuid(data + 5);
    stream.formatIndex = data[3];
    stream.frameCount = data[4];
    stream.bitsPerPixel = data[21];
    stream.defaultFrameIndex = data[22];
    return true;
  }
  return false;
}

// Decodes a VS_FRAME_UNCOMPRESSED (0x05) or VS_FRAME_MJPEG (0x07) class
// descriptor into the frame half of a stream. Every length check below exists
// because bFrameIntervalType is attacker-adjacent data: a camera that claims
// more intervals than bLength covers would otherwise have them read from
// whatever follows the descriptor in the configuration buffer.
inline bool espUsbHostVideoDecodeFrameDescriptor(const uint8_t *data,
                                                 EspUsbHostVideoStreamInfo &stream)
{
  // 26 bytes of fixed fields: bLength, CS_INTERFACE, subtype, bFrameIndex,
  // bmCapabilities, wWidth, wHeight, dwMinBitRate, dwMaxBitRate,
  // dwMaxVideoFrameBufferSize, dwDefaultFrameInterval, bFrameIntervalType.
  if (!data || data[0] < 26 || data[1] != 0x24)
  {
    return false;
  }
  if (data[2] != 0x05 && data[2] != 0x07)
  {
    return false;
  }
  const uint8_t intervalType = data[25];
  const size_t needed = intervalType == 0
                            ? 26u + 12u
                            : 26u + static_cast<size_t>(intervalType) * 4u;
  if (data[0] < needed)
  {
    return false;
  }

  stream.frameIndex = data[3];
  stream.width = espUsbHostVideoReadU16(data + 5);
  stream.height = espUsbHostVideoReadU16(data + 7);
  stream.maxVideoFrameBufferSize = espUsbHostVideoReadU32(data + 17);
  stream.frameInterval = espUsbHostVideoReadU32(data + 21);
  stream.frameIntervalCount = 0;
  stream.frameIntervalMin = 0;
  stream.frameIntervalMax = 0;
  stream.frameIntervalStep = 0;
  for (size_t i = 0; i < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS; i++)
  {
    stream.frameIntervals[i] = 0;
  }

  if (intervalType == 0)
  {
    stream.frameIntervalMin = espUsbHostVideoReadU32(data + 26);
    stream.frameIntervalMax = espUsbHostVideoReadU32(data + 30);
    stream.frameIntervalStep = espUsbHostVideoReadU32(data + 34);
    return true;
  }

  // A camera may advertise more rates than the fixed array holds. Keeping the
  // first few and reporting the frame is better than dropping a frame size the
  // caller asked for: the rates kept are the ones the device listed first, which
  // by convention are its fastest.
  const size_t count = intervalType < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS
                           ? static_cast<size_t>(intervalType)
                           : ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS;
  for (size_t i = 0; i < count; i++)
  {
    stream.frameIntervals[i] = espUsbHostVideoReadU32(data + 26 + i * 4);
  }
  stream.frameIntervalCount = static_cast<uint8_t>(count);
  return true;
}

inline bool espUsbHostVideoStreamSupportsFrameInterval(const EspUsbHostVideoStreamInfo &stream,
                                                       uint32_t frameInterval)
{
  if (frameInterval == 0)
  {
    return true;
  }
  if (stream.frameIntervalCount > 0)
  {
    for (uint8_t i = 0;
         i < stream.frameIntervalCount && i < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS;
         i++)
    {
      if (stream.frameIntervals[i] == frameInterval)
      {
        return true;
      }
    }
    return false;
  }
  if (stream.frameIntervalMin == 0 && stream.frameIntervalMax == 0)
  {
    // Neither form was decoded: all that is known is the default rate.
    return stream.frameInterval == 0 || stream.frameInterval == frameInterval;
  }
  if (frameInterval < stream.frameIntervalMin || frameInterval > stream.frameIntervalMax)
  {
    return false;
  }
  // The endpoints are always offered. Cameras routinely advertise a
  // dwMaxFrameInterval that is not min + n * step, and rejecting it would deny
  // the slowest rate the device itself named.
  if (frameInterval == stream.frameIntervalMin || frameInterval == stream.frameIntervalMax)
  {
    return true;
  }
  if (stream.frameIntervalStep == 0)
  {
    return true;
  }
  return ((frameInterval - stream.frameIntervalMin) % stream.frameIntervalStep) == 0;
}

// The interval this stream would actually run at for a requested one. Zero means
// the caller has no preference, which resolves to the device's own default.
inline uint32_t espUsbHostVideoStreamNearestFrameInterval(const EspUsbHostVideoStreamInfo &stream,
                                                          uint32_t desired)
{
  if (desired == 0)
  {
    return stream.frameInterval;
  }
  if (stream.frameIntervalCount > 0)
  {
    uint32_t best = 0;
    uint32_t bestDelta = 0;
    for (uint8_t i = 0;
         i < stream.frameIntervalCount && i < ESP_USB_HOST_MAX_VIDEO_FRAME_INTERVALS;
         i++)
    {
      const uint32_t candidate = stream.frameIntervals[i];
      if (candidate == 0)
      {
        continue;
      }
      const uint32_t delta = candidate > desired ? candidate - desired : desired - candidate;
      if (best == 0 || delta < bestDelta)
      {
        best = candidate;
        bestDelta = delta;
      }
    }
    return best;
  }
  if (stream.frameIntervalMin == 0 && stream.frameIntervalMax == 0)
  {
    return stream.frameInterval;
  }
  if (desired <= stream.frameIntervalMin)
  {
    return stream.frameIntervalMin;
  }
  if (desired >= stream.frameIntervalMax)
  {
    return stream.frameIntervalMax;
  }
  if (stream.frameIntervalStep == 0)
  {
    return desired;
  }
  const uint32_t offset = desired - stream.frameIntervalMin;
  const uint32_t steps = (offset + stream.frameIntervalStep / 2) / stream.frameIntervalStep;
  const uint32_t value = stream.frameIntervalMin + steps * stream.frameIntervalStep;
  return value > stream.frameIntervalMax ? stream.frameIntervalMax : value;
}

// Ranks one stream against a request. A zero width, height or frameInterval is a
// wildcard; a non-zero one must match exactly, because a caller that sized a
// frame buffer for what it asked for would overflow it if the host quietly
// substituted a larger frame. Returns -1 for a stream that cannot serve the
// request at all.
inline int espUsbHostVideoStreamScore(const EspUsbHostVideoStreamInfo &stream,
                                      uint16_t width,
                                      uint16_t height,
                                      uint32_t frameInterval)
{
  if (!stream.startable || stream.width == 0 || stream.height == 0)
  {
    return -1;
  }
  if (width != 0 && stream.width != width)
  {
    return -1;
  }
  if (height != 0 && stream.height != height)
  {
    return -1;
  }
  if (!espUsbHostVideoStreamSupportsFrameInterval(stream, frameInterval))
  {
    return -1;
  }
  // Pixel count dominates, so an unconstrained request gets the largest frame the
  // camera offers. The format bonus only ever breaks ties between two streams of
  // the same size, where MJPEG is the one an MCU host wants: it costs a fraction
  // of the bandwidth and of the frame buffer an uncompressed stream needs.
  int score = static_cast<int>(stream.width) * static_cast<int>(stream.height);
  if (stream.format == ESP_USB_HOST_VIDEO_FORMAT_MJPEG)
  {
    score += 2;
  }
  else if (stream.format != ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN)
  {
    score += 1;
  }
  return score;
}

// Picks the stream that best serves a request. Pass
// ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0 to take whatever the camera offers.
// fps is a frame rate, not an interval: the conversion is done here so callers
// never have to write 10000000 / n.
inline EspUsbHostVideoStreamSelection espUsbHostSelectVideoStream(const EspUsbHostVideoStreamInfo *streams,
                                                                  size_t count,
                                                                  uint8_t format,
                                                                  uint16_t width,
                                                                  uint16_t height,
                                                                  uint32_t fps)
{
  EspUsbHostVideoStreamSelection best;
  if (!streams)
  {
    return best;
  }
  const uint32_t requested = espUsbHostVideoFpsToFrameInterval(fps);
  for (size_t i = 0; i < count; i++)
  {
    const EspUsbHostVideoStreamInfo &stream = streams[i];
    if (format != ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN && stream.format != format)
    {
      continue;
    }
    // With no rate named, the stream runs at its own default.
    const uint32_t frameInterval = requested != 0 ? requested : stream.frameInterval;
    const int score = espUsbHostVideoStreamScore(stream, width, height, frameInterval);
    if (score < 0)
    {
      continue;
    }
    if (best.index < 0 || score > best.score)
    {
      best.index = static_cast<int>(i);
      best.frameInterval = frameInterval;
      best.score = score;
    }
  }
  return best;
}

// Decodes the payload header that prefixes every non-empty packet from the
// streaming endpoint. Returns false for a packet that carries no usable header,
// including the zero-length packets a camera sends when it has nothing ready --
// those are normal and not an error.
inline bool espUsbHostVideoDecodePayloadHeader(const uint8_t *data,
                                               size_t length,
                                               EspUsbHostVideoPayloadHeader &header)
{
  if (!data || length < 2)
  {
    return false;
  }
  const uint8_t headerLength = data[0];
  if (headerLength < 2 || static_cast<size_t>(headerLength) > length)
  {
    return false;
  }
  const uint8_t info = data[1];
  // bHeaderLength has to cover the optional fields bmHeaderInfo claims. A header
  // that does not is rejected rather than parsed: the PTS and SCR would be read
  // out of the image bytes, and payloadOffset would then point into the middle of
  // the header.
  size_t needed = 2;
  if ((info & 0x04) != 0)
  {
    needed += 4;
  }
  if ((info & 0x08) != 0)
  {
    needed += 6;
  }
  if (static_cast<size_t>(headerLength) < needed)
  {
    return false;
  }

  header = EspUsbHostVideoPayloadHeader();
  header.headerLength = headerLength;
  header.info = info;
  header.frameId = (info & 0x01) != 0;
  header.endOfFrame = (info & 0x02) != 0;
  header.stillImage = (info & 0x20) != 0;
  header.error = (info & 0x40) != 0;
  header.endOfHeader = (info & 0x80) != 0;

  size_t offset = 2;
  if ((info & 0x04) != 0)
  {
    header.hasPresentationTime = true;
    header.presentationTime = espUsbHostVideoReadU32(data + offset);
    offset += 4;
  }
  if ((info & 0x08) != 0)
  {
    header.hasSourceClock = true;
    header.sourceClock = espUsbHostVideoReadU32(data + offset);
    header.sourceClockCounter = espUsbHostVideoReadU16(data + offset + 4);
  }
  // Image data starts at bHeaderLength, not at the end of the fields decoded
  // above: a camera may pad the header beyond what its flags require.
  header.payloadOffset = headerLength;
  return true;
}

// Serialises a Probe/Commit payload. length is the negotiated control length, so
// pass what GET_LEN reported: a UVC 1.0 camera stalls a 34-byte SET_CUR. Writes
// at most the UVC 1.1 form -- this host has no use for the 1.5-only fields --
// and returns the number of bytes written, or 0 if length is too small to hold
// even the 1.0 form.
inline size_t espUsbHostVideoEncodeProbeControl(const EspUsbHostVideoProbeControl &control,
                                                uint8_t *out,
                                                size_t length)
{
  if (!out || length < ESP_USB_HOST_VIDEO_PROBE_LENGTH_10)
  {
    return 0;
  }
  const size_t written = length < ESP_USB_HOST_VIDEO_PROBE_LENGTH_11
                             ? ESP_USB_HOST_VIDEO_PROBE_LENGTH_10
                             : ESP_USB_HOST_VIDEO_PROBE_LENGTH_11;
  for (size_t i = 0; i < written; i++)
  {
    out[i] = 0;
  }
  espUsbHostVideoWriteU16(out + 0, control.hint);
  out[2] = control.formatIndex;
  out[3] = control.frameIndex;
  espUsbHostVideoWriteU32(out + 4, control.frameInterval);
  espUsbHostVideoWriteU16(out + 8, control.keyFrameRate);
  espUsbHostVideoWriteU16(out + 10, control.pFrameRate);
  espUsbHostVideoWriteU16(out + 12, control.compQuality);
  espUsbHostVideoWriteU16(out + 14, control.compWindowSize);
  espUsbHostVideoWriteU16(out + 16, control.delay);
  espUsbHostVideoWriteU32(out + 18, control.maxVideoFrameSize);
  espUsbHostVideoWriteU32(out + 22, control.maxPayloadTransferSize);
  if (written >= ESP_USB_HOST_VIDEO_PROBE_LENGTH_11)
  {
    espUsbHostVideoWriteU32(out + 26, control.clockFrequency);
    out[30] = control.framingInfo;
    out[31] = control.preferredVersion;
    out[32] = control.minVersion;
    out[33] = control.maxVersion;
  }
  return written;
}

// Parses a Probe/Commit reply. Fields the reply is too short to carry read back
// as zero rather than as whatever the transfer buffer held before it.
inline bool espUsbHostVideoDecodeProbeControl(const uint8_t *data,
                                              size_t length,
                                              EspUsbHostVideoProbeControl &control)
{
  if (!data || length < ESP_USB_HOST_VIDEO_PROBE_LENGTH_10)
  {
    return false;
  }
  control = EspUsbHostVideoProbeControl();
  control.hint = espUsbHostVideoReadU16(data + 0);
  control.formatIndex = data[2];
  control.frameIndex = data[3];
  control.frameInterval = espUsbHostVideoReadU32(data + 4);
  control.keyFrameRate = espUsbHostVideoReadU16(data + 8);
  control.pFrameRate = espUsbHostVideoReadU16(data + 10);
  control.compQuality = espUsbHostVideoReadU16(data + 12);
  control.compWindowSize = espUsbHostVideoReadU16(data + 14);
  control.delay = espUsbHostVideoReadU16(data + 16);
  control.maxVideoFrameSize = espUsbHostVideoReadU32(data + 18);
  control.maxPayloadTransferSize = espUsbHostVideoReadU32(data + 22);
  if (length >= ESP_USB_HOST_VIDEO_PROBE_LENGTH_11)
  {
    control.clockFrequency = espUsbHostVideoReadU32(data + 26);
    control.framingInfo = data[30];
    control.preferredVersion = data[31];
    control.minVersion = data[32];
    control.maxVersion = data[33];
  }
  else
  {
    control.clockFrequency = 0;
    control.framingInfo = 0;
    control.preferredVersion = 0;
    control.minVersion = 0;
    control.maxVersion = 0;
  }
  return true;
}

void espUsbHostPrintHex(const uint8_t *data, size_t length, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostInterfaceInfo &intf, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostNetworkInterfaceInfo &network, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostSerialPortInfo &port, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostVideoStreamInfo &stream, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostHIDReportDescriptor &descriptor, Print &out = Serial);
void espUsbHostPrintHIDReportDescriptor(const uint8_t *data, size_t length, Print &out = Serial);
const char *espUsbHostConsumerControlUsageName(uint16_t usage);
const char *espUsbHostSystemControlUsageName(uint8_t usage);
const char *espUsbHostNetworkProtocolName(EspUsbHostNetworkProtocol protocol);

struct EspUsbHostConsumerControlEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint16_t usage = 0;
  bool pressed = false;
  bool released = false;
};

struct EspUsbHostGamepadEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const EspUsbHostHIDFieldValue *fields = nullptr;
  size_t fieldCount = 0;
  bool changed = false;
};

struct EspUsbHostGamepadPrevState
{
  uint8_t reportData[ESP_USB_HOST_GAMEPAD_MAX_REPORT_BYTES] = {};
  size_t reportLength = 0;
};

struct EspUsbHostHIDVendorInput : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
};

struct EspUsbHostSystemControlEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t usage = 0;
  bool pressed = false;
  bool released = false;
};

class EspUsbHostCdcSerial;

class EspUsbHost
{
public:
  using DeviceCallback = std::function<void(const EspUsbHostDeviceInfo &)>;
  using KeyboardCallback = std::function<void(const EspUsbHostKeyboardEvent &)>;
  using KeyboardStateCallback = std::function<void(const EspUsbHostKeyboardState &)>;
  using MouseCallback = std::function<void(const EspUsbHostMouseEvent &)>;
  using HIDInputCallback = std::function<void(const EspUsbHostHIDInput &)>;
  using HIDReportDescriptorCallback = std::function<void(const EspUsbHostHIDReportDescriptor &)>;
  using SerialDataCallback = std::function<void(const EspUsbHostSerialData &)>;
  using MidiMessageCallback = std::function<void(const EspUsbHostMidiMessage &)>;
  using VideoFrameCallback = std::function<void(const EspUsbHostVideoFrame &)>;
  using AudioDataCallback = std::function<void(const EspUsbHostAudioData &)>;
  using AudioOutputCallback = std::function<void(EspUsbHostAudioOutputRequest &)>;
  using ConsumerControlCallback = std::function<void(const EspUsbHostConsumerControlEvent &)>;
  using GamepadCallback = std::function<void(const EspUsbHostGamepadEvent &)>;
  using HIDVendorInputCallback = std::function<void(const EspUsbHostHIDVendorInput &)>;
  using VendorDataCallback = std::function<void(const EspUsbHostVendorData &)>;
  using CcidSlotChangeCallback = std::function<void(const EspUsbHostCcidSlotEvent &)>;
  using SystemControlCallback = std::function<void(const EspUsbHostSystemControlEvent &)>;
  using NetworkFrameCallback = std::function<void(const EspUsbHostNetworkFrame &)>;
  // Return 0 to keep the device's default configuration, or a configuration
  // value in the range 1..bNumConfigurations. Called from the USB Host library
  // task during enumeration, so the callback must not block.
  using ConfigurationSelector = std::function<uint8_t(const usb_device_desc_t &)>;
  static constexpr size_t MaxListenersPerEvent = ESP_USB_HOST_MAX_LISTENERS_PER_EVENT;
  static_assert(MaxListenersPerEvent > 0, "ESP_USB_HOST_MAX_LISTENERS_PER_EVENT must be greater than zero");
  static constexpr size_t MaxLifecycleListeners = ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS;
  static_assert(MaxLifecycleListeners > 0, "ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS must be greater than zero");

  EspUsbHost();
  ~EspUsbHost();

  bool begin();
  bool begin(const EspUsbHostConfig &config);
  void end();
  bool ready() const;
  bool setConfigurationSelector(ConfigurationSelector selector);

  void onDeviceConnected(DeviceCallback callback);
  void onDeviceDisconnected(DeviceCallback callback);
  void onKeyboard(KeyboardCallback callback);
  void onKeyboardState(KeyboardStateCallback callback);
  void onMouse(MouseCallback callback);
  void onHIDInput(HIDInputCallback callback);
  void onHIDReportDescriptor(HIDReportDescriptorCallback callback);
  void onSerialData(SerialDataCallback callback);
  void onMidiMessage(MidiMessageCallback callback);
  void onVideoFrame(VideoFrameCallback callback);
  void onAudioData(AudioDataCallback callback);
  void onAudioOutputRequest(AudioOutputCallback callback);
  void onConsumerControl(ConsumerControlCallback callback);
  void onGamepad(GamepadCallback callback);
  void onHIDVendorInput(HIDVendorInputCallback callback);
  void onVendorData(VendorDataCallback callback);
  void onSystemControl(SystemControlCallback callback);
  void onNetworkFrame(NetworkFrameCallback callback);
  EspUsbHostListenerId addKeyboardListener(KeyboardCallback callback);
  EspUsbHostListenerId addKeyboardStateListener(KeyboardStateCallback callback);
  EspUsbHostListenerId addMouseListener(MouseCallback callback);
  EspUsbHostListenerId addConsumerControlListener(ConsumerControlCallback callback);
  EspUsbHostListenerId addSystemControlListener(SystemControlCallback callback);
  EspUsbHostListenerId addGamepadListener(GamepadCallback callback);
  // Device lifecycle and MIDI listeners. Same contract as the input listeners
  // above: the single on*() callback stays compatible and runs first, listeners
  // run in registration order from a per-event snapshot, removal is by id, and
  // add / remove from inside a callback takes effect on the next event.
  // Lifecycle listeners share the ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS budget.
  EspUsbHostListenerId addDeviceConnectedListener(DeviceCallback callback);
  EspUsbHostListenerId addDeviceDisconnectedListener(DeviceCallback callback);
  EspUsbHostListenerId addMidiMessageListener(MidiMessageCallback callback);
  bool removeListener(EspUsbHostListenerId listenerId);

  void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
  bool sendSetProtocol(uint8_t interfaceNumber, uint8_t address);
  bool sendHIDReport(uint8_t interfaceNumber,
                     uint8_t reportType,
                     uint8_t reportId,
                     const uint8_t *data,
                     size_t length,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendHIDVendorOutput(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendHIDVendorFeature(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Claims a bulk interface for direct transfers. With the default
  // interfaceNumber the first vendor-specific (class 0xFF) interface is chosen.
  // Naming an interface explicitly claims it whatever its class, for devices
  // whose bulk protocol sits behind some other class code; an interface already
  // claimed by another part of this library is still refused.
  //
  // readTransferBytes sizes one continuous IN transfer. The default 0 keeps the
  // historical behaviour of one endpoint-sized packet per transfer, which keeps
  // every short message its own onVendorData() callback but leaves the endpoint
  // idle between transfers: a streaming device is then limited by the
  // per-transfer turnaround rather than by the bus. A larger size is rounded up
  // to a whole number of max-size packets and capped at
  // ESP_USB_HOST_VENDOR_READ_MAX_TRANSFER_BYTES. A short packet still ends the
  // transfer early, so message boundaries survive; back-to-back full packets are
  // delivered as one callback instead of many.
  bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t interfaceNumber = 0xff,
                  EspUsbHostVendorReadMode readMode = ESP_USB_HOST_VENDOR_READ_CONTINUOUS,
                  size_t readTransferBytes = 0);
  bool vendorWrite(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  size_t vendorRead(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // One bulk IN transfer, submitted now and waited for. This is the read a
  // request/response protocol wants, and the only one available after opening
  // with ESP_USB_HOST_VENDOR_READ_ON_DEMAND. Like vendorWrite() it waits for
  // completion, so it cannot be called from a USB callback. Returns false on
  // timeout or transfer error; actualLength receives the bytes copied out.
  bool vendorReadSync(uint8_t *buffer,
                      size_t length,
                      size_t *actualLength = nullptr,
                      uint32_t timeoutMs = ESP_USB_HOST_VENDOR_READ_DEFAULT_TIMEOUT_MS,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Max packet size of the bulk OUT endpoint opened by vendorOpen(), or 0 when
  // no vendor interface is open. Callers that must terminate a transfer on a
  // packet boundary need this value.
  uint16_t vendorOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint16_t vendorInPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Bytes one continuous bulk IN transfer asks for, as vendorOpen() rounded the
  // requested size. 0 when no vendor interface is open or reads are on-demand.
  size_t vendorInTransferBytes(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Address of the endpoints vendorOpen() selected, or 0 when none is open. An
  // interface can expose several bulk endpoints per direction, so a caller that
  // requires a specific one needs to check which was chosen.
  uint8_t vendorOutEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint8_t vendorInEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

  // Asynchronous bulk OUT queue. vendorWrite() waits for each transfer to
  // complete, which leaves the bus idle between transfers; the queue keeps
  // several transfers in flight instead. Unlike vendorWrite(), these calls never
  // wait for completion and may be used from USB callbacks.
  //
  // Preferred (zero-copy) sequence: acquire a pooled DMA buffer, write the
  // payload into it, then submit it.
  //
  //   size_t capacity = 0;
  //   uint8_t *buffer = usb.vendorWriteAcquire(&capacity, 100);
  //   if (buffer) { size_t n = encode(buffer, capacity); usb.vendorWriteSubmit(buffer, n); }
  bool vendorWriteQueueBegin(size_t depth,
                             size_t bufferBytes,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void vendorWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool vendorWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint8_t *vendorWriteAcquire(size_t *capacity,
                              uint32_t timeoutMs = 0,
                              uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool vendorWriteSubmit(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void vendorWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Copies into a pooled buffer and submits it. Fails when length exceeds the
  // per-slot buffer size; the caller decides how to split.
  bool vendorWriteAsync(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  size_t vendorWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t vendorWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool vendorWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  EspUsbHostVendorWriteStats vendorWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  void vendorWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

  // Asynchronous bulk IN queue, the receive side of the queue above. The
  // continuous read vendorOpen() sets up keeps one transfer outstanding and only
  // submits the next one after the completion has been handled, so the endpoint
  // is idle for the turnaround of every transfer. The queue keeps `depth`
  // transfers of `bufferBytes` outstanding instead and resubmits each from its
  // own completion, which leaves the device with a token to answer at all times.
  //
  // Data still arrives through onVendorData() and vendorRead(); only the shape of
  // the transfers underneath changes. bufferBytes is rounded up to a whole number
  // of max-size packets and capped at ESP_USB_HOST_VENDOR_READ_MAX_TRANSFER_BYTES.
  //
  // Beginning the queue takes the endpoint over from the continuous read, which
  // means waiting for the outstanding transfer to be canceled: call it from a
  // normal task, not from a USB callback. It is refused on an interface opened
  // with ESP_USB_HOST_VENDOR_READ_ON_DEMAND, where vendorReadSync() owns the
  // endpoint instead.
  bool vendorReadQueueBegin(size_t depth,
                            size_t bufferBytes,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Stops the queue and restores nothing: the endpoint is left idle, so reads
  // continue with vendorReadSync() or with another vendorReadQueueBegin().
  void vendorReadQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool vendorReadQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Transfers currently outstanding on the endpoint, 0..depth.
  size_t vendorReadPending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  EspUsbHostVendorReadStats vendorReadStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  void vendorReadStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

  // Bulk OUT packet boundaries. A transfer whose length is a multiple of the
  // endpoint max packet size does not terminate the USB transfer by itself; some
  // protocols (ADB, CDC-NCM) require a following zero-length packet.
  bool vendorWriteZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Off by default. When enabled, every vendor bulk OUT write whose length is a
  // non-zero multiple of the max packet size is followed by a ZLP. With the
  // async queue this consumes a second slot, so use a depth of at least 2.
  void vendorSetAutoZlp(bool enable, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool vendorAutoZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool vendorControlIn(uint8_t request,
                       uint16_t value,
                       uint16_t index,
                       uint8_t *data,
                       size_t length,
                       size_t *actualLength = nullptr,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                       uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
  bool vendorControlOut(uint8_t request,
                        uint16_t value,
                        uint16_t index,
                        const uint8_t *data = nullptr,
                        size_t length = 0,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
  // One EP0 control transfer with a caller-supplied bmRequestType. The two calls
  // above cover vendor requests addressed to the device; this is the escape hatch
  // for class or standard requests and for interface or endpoint recipients, which
  // is what a protocol layered on a non-vendor class needs. USBTMC, for example,
  // sends its class requests as 0xa1 / 0x21 with wIndex set to the interface, and
  // clears a halted bulk endpoint with the standard 0x02 / CLEAR_FEATURE.
  //
  // The transfer direction comes from bit 7 of requestType; actualLength receives
  // the bytes received on an IN transfer. Like the calls above it waits for
  // completion, so it cannot be called from a USB callback.
  bool vendorControlTransfer(uint8_t requestType,
                             uint8_t request,
                             uint16_t value,
                             uint16_t index,
                             uint8_t *data = nullptr,
                             size_t length = 0,
                             size_t *actualLength = nullptr,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
  // Every serial call takes the device address first and the port within that
  // device second. A composite device can publish more than one CDC-ACM function
  // (two USB-serial ports over one cable); ports are numbered from 0 in the order
  // their functions appear in the configuration descriptor. Leaving port at
  // ESP_USB_HOST_ANY_PORT picks the device's first ready port, which is what a
  // single-port device -- and every sketch written before multi-port support --
  // gets.
  bool sendSerial(const uint8_t *data,
                  size_t length,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t port = ESP_USB_HOST_ANY_PORT);
  bool sendSerial(const char *text,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t port = ESP_USB_HOST_ANY_PORT);
  bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  // How many CDC-ACM (or vendor VCP) ports the host took a control interface for.
  // 0 when the device has no serial function, or when they all fell past
  // ESP_USB_HOST_MAX_SERIAL_PORTS. A port whose data interface or endpoints did
  // not come up is still counted here; getSerialPortInfo().ready is what says
  // whether a port can carry data.
  uint8_t serialPortCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Interface and endpoint numbers behind one port index, so a sketch can match a
  // port against the descriptor (or against an iInterface name it read itself).
  bool getSerialPortInfo(EspUsbHostSerialPortInfo &info,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  bool setSerialBaudRate(uint32_t baud,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t port = ESP_USB_HOST_ANY_PORT);
  bool setSerialConfig(const EspUsbHostSerialConfig &config,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                       uint8_t port = ESP_USB_HOST_ANY_PORT);
  // Max packet size of the CDC data OUT endpoint, or 0 when no serial device is
  // ready. Needed by callers that must terminate a transfer on a packet boundary.
  uint16_t serialOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                               uint8_t port = ESP_USB_HOST_ANY_PORT) const;

  // Asynchronous CDC OUT queue. Without it, sendSerial() allocates a transfer per
  // call and never applies backpressure, so a writer that outruns the bus grows
  // the in-flight set until DMA memory runs out. The queue preallocates a fixed
  // pool instead: submits are still non-blocking, but an acquire blocks once the
  // pool is busy, which is what paces a bulk producer such as a display.
  //
  // While the queue is active sendSerial() (and EspUsbHostCdcSerial::write())
  // route through it, so existing code gets the backpressure without changes.
  // Writes longer than the per-slot buffer still take the one-shot path.
  //
  // Preferred (zero-copy) sequence: acquire a pooled DMA buffer, fill it, submit.
  //
  //   size_t capacity = 0;
  //   uint8_t *buffer = usb.serialWriteAcquire(&capacity, 100);
  //   if (buffer) { size_t n = encode(buffer, capacity); usb.serialWriteSubmit(buffer, n); }
  //
  // The queue belongs to one port, not to the device: each port has its own OUT
  // endpoint, so a two-port device that wants backpressure on both calls
  // serialWriteQueueBegin() once per port and pays for two transfer pools.
  bool serialWriteQueueBegin(size_t depth,
                             size_t bufferBytes,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t port = ESP_USB_HOST_ANY_PORT);
  void serialWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint8_t port = ESP_USB_HOST_ANY_PORT);
  bool serialWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  uint8_t *serialWriteAcquire(size_t *capacity,
                              uint32_t timeoutMs = 0,
                              uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                              uint8_t port = ESP_USB_HOST_ANY_PORT);
  bool serialWriteSubmit(uint8_t *buffer,
                         size_t length,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t port = ESP_USB_HOST_ANY_PORT);
  void serialWriteRelease(uint8_t *buffer,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint8_t port = ESP_USB_HOST_ANY_PORT);
  // Copies into a pooled buffer and submits it. Fails when length exceeds the
  // per-slot buffer size; the caller decides how to split.
  bool serialWriteAsync(const uint8_t *data,
                        size_t length,
                        uint32_t timeoutMs = 0,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint8_t port = ESP_USB_HOST_ANY_PORT);
  size_t serialWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                            uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  size_t serialWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                              uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  bool serialWriteFlush(uint32_t timeoutMs,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint8_t port = ESP_USB_HOST_ANY_PORT);
  EspUsbHostSerialWriteStats serialWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                                              uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  void serialWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t port = ESP_USB_HOST_ANY_PORT);

  bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

  // Cable configuration of a connected MIDI device, known from the descriptors
  // as soon as it enumerates. False when the address has no MIDI Streaming
  // interface or is not connected.
  //
  // Only the first MIDI Streaming interface of a device is tracked, and within
  // it one bulk endpoint per direction, which is the same interface midiSend()
  // and the message callbacks work with.
  bool getMidiPortInfo(EspUsbHostMidiPortInfo &info,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool audioInputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool audioInputStart(uint8_t channels,
                       uint8_t bitsPerSample,
                       uint32_t sampleRate,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioInputStart(const EspUsbHostAudioStreamInfo &stream,
                       uint32_t sampleRate = 0,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool setAudioSampleRate(uint32_t sampleRate, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioOutputStart(uint8_t channels,
                        uint8_t bitsPerSample,
                        uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioOutputStart(const EspUsbHostAudioStreamInfo &stream,
                        uint32_t sampleRate = 0,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void audioOutputStop(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioOutputRunning(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint32_t audioOutputUnderruns(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // True when the running playback stream has an explicit feedback endpoint.
  bool audioOutputHasFeedback(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Rate the device last asked for through its feedback endpoint, or 0 when it has
  // none and playback runs at the negotiated rate.
  uint32_t audioOutputFeedbackRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Accepted and rejected feedback packets. A rising reject count means the device
  // reports rates outside the plausible window, which are ignored.
  uint32_t audioOutputFeedbackUpdates(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint32_t audioOutputFeedbackRejects(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Rate playback is actually paced at: the feedback rate once one has arrived,
  // otherwise the negotiated rate.
  uint32_t audioOutputRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool audioSend(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  size_t getAudioFeatureUnits(uint8_t address, EspUsbHostAudioFeatureUnitInfo *units, size_t maxUnits) const;
  bool audioHasMute(uint8_t address = ESP_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0, uint8_t channel = 0) const;
  bool audioHasVolume(uint8_t address = ESP_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0, uint8_t channel = 0) const;
  bool audioGetMute(bool &mute,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0,
                    uint8_t channel = 0,
                    uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioSetMute(bool mute,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0,
                    uint8_t channel = 0,
                    uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioGetVolume(int16_t &volume,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0,
                      uint8_t channel = 0,
                      uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioSetVolume(int16_t volume,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0,
                      uint8_t channel = 0,
                      uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioGetVolumeRange(EspUsbHostAudioVolumeRange &range,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint8_t unitId = 0,
                           uint8_t channel = 0,
                           uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioGetVolumeDb(float &db,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint8_t unitId = 0,
                        uint8_t channel = 0,
                        uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioSetVolumeDb(float db,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint8_t unitId = 0,
                        uint8_t channel = 0,
                        uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioSetVolumeDbClamped(float db,
                               uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                               uint8_t unitId = 0,
                               uint8_t channel = 0,
                               uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioConfigureVolume(float db,
                            bool mute = false,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                            uint8_t unitId = 0,
                            uint8_t channel = 0,
                            uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioSetVolumePercent(uint8_t percent,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t unitId = 0,
                             uint8_t channel = 0,
                             uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool audioConfigureVolumePercent(uint8_t percent,
                                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                                   uint8_t unitId = 0,
                                   uint8_t channel = 0,
                                   uint32_t timeoutMs = ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS);
  bool mscReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool mscInquiry(EspUsbHostMscInquiry &inquiry,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscRequestSense(EspUsbHostMscSense &sense,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                       uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscLastSense(EspUsbHostMscSense &sense,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool mscMaxLun(uint8_t &maxLun,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscSelectLun(uint8_t lun,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscGetBlockDeviceInfo(EspUsbHostMscBlockDeviceInfo &info,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscWaitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t readyTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
                    uint32_t commandTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscCapacity64(uint64_t &blockCount,
                     uint32_t &blockSize,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscCapacity(uint32_t &blockCount,
                   uint32_t &blockSize,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscReadBlocks(uint32_t lba,
                     uint8_t *data,
                     uint32_t blockCount,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscWriteBlocks(uint32_t lba,
                      const uint8_t *data,
                      uint32_t blockCount,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscReadBlocks64(uint64_t lba,
                       uint8_t *data,
                       uint32_t blockCount,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                       uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscWriteBlocks64(uint64_t lba,
                        const uint8_t *data,
                        uint32_t blockCount,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscSynchronizeCache(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
  bool mscMount(const char *basePath = "/usb",
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t lun = 0,
                uint8_t maxFiles = 4,
                uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
                bool skipSyncCache = false);
  bool mscUnmount(const char *basePath = "/usb");
  bool mscMounted(const char *basePath = "/usb") const;

  // CCID smart card readers (bInterfaceClass 0x0b, bulk protocol 0x00).
  //
  // The interface is not claimed during enumeration; ccidOpen() claims it and
  // starts the slot-change notifications. Every call below that talks to the
  // reader waits for the transfer to complete, so none of them may be called
  // from a USB callback (they return false there), same as the MSC and vendor
  // bulk APIs.
  bool ccidOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t interfaceNumber = 0xff);
  // Stops CCID activity and frees the message buffer. The interface stays
  // claimed until the device disconnects, so a later ccidOpen() can reuse it.
  void ccidClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool ccidReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool ccidGetInterface(EspUsbHostCcidInterface &info,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint8_t ccidSlotCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

  bool ccidGetStatus(EspUsbHostCcidStatus &status,
                     uint8_t slot = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = 1000);
  bool ccidCardPresent(uint8_t slot = 0,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

  // Activates the card and returns its ATR. The ATR is cached until power off,
  // card removal, or disconnect; ccidGetAtr() returns the cached copy.
  bool ccidPowerOn(uint8_t *atr = nullptr,
                   size_t atrCapacity = 0,
                   size_t *atrLength = nullptr,
                   EspUsbHostCcidVoltage voltage = ESP_USB_HOST_CCID_VOLTAGE_AUTO,
                   uint8_t slot = 0,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
  bool ccidPowerOff(uint8_t slot = 0,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = 2000);
  size_t ccidGetAtr(uint8_t *buffer,
                    size_t capacity,
                    uint8_t slot = 0,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Card standard (ISO 14443 A/B, ISO 15693, FeliCa, ...) and card name decoded
  // from the ATR that ccidPowerOn() cached. False when no card is activated or
  // the ATR cannot be parsed. See EspUsbHostCcidAtr.h for what an ATR can and
  // cannot say about the card.
  bool ccidGetCardInfo(EspUsbHostCcidCardInfo &info,
                       uint8_t slot = 0,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // ccidGetCardInfo() plus a fallback for cards the ATR does not identify: the
  // PC/SC Get UID pseudo APDU is sent and the standard is inferred from the
  // identifier's shape (info.fromUid is then true). A FeliCa card, for example,
  // gets an ATR with no historical bytes from a CCID reader, so the 8-byte IDm
  // is the only thing left to go on. Unlike ccidGetCardInfo() this talks to the
  // card, so it needs an activated card and cannot run from a USB callback.
  bool ccidIdentifyCard(EspUsbHostCcidCardInfo &info,
                        uint8_t slot = 0,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);

  // PC_to_RDR_XfrBlock. Payload and response are passed through untouched.
  bool ccidTransfer(const uint8_t *tx,
                    size_t txLength,
                    uint8_t *rx,
                    size_t rxCapacity,
                    size_t *rxLength,
                    uint8_t slot = 0,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
  // ccidTransfer() plus SW1SW2 splitting: response/responseLength exclude the
  // status word, which is returned through statusWord. 61xx / 6Cxx are reported
  // as-is; the caller decides whether to reissue.
  bool ccidApdu(const uint8_t *apdu,
                size_t apduLength,
                uint8_t *response,
                size_t responseCapacity,
                size_t *responseLength,
                uint16_t *statusWord = nullptr,
                uint8_t slot = 0,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
  // PC_to_RDR_Escape, for reader-specific commands.
  bool ccidEscape(const uint8_t *tx,
                  size_t txLength,
                  uint8_t *rx,
                  size_t rxCapacity,
                  size_t *rxLength,
                  uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);

  // Any PC_to_RDR message, for the ones this API does not wrap (SetParameters,
  // IccClock, T0APDU, ...). messageSpecific is header bytes 7..9; nullptr means
  // three zero bytes.
  bool ccidMessage(uint8_t messageType,
                   const uint8_t *messageSpecific,
                   const uint8_t *data,
                   size_t length,
                   EspUsbHostCcidResponse &response,
                   uint8_t slot = 0,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);

  // CCID class request ABORT followed by PC_to_RDR_Abort, per the CCID spec.
  bool ccidAbort(uint8_t slot = 0,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = 1000);
  // bError of the last failed response (0 when the last call succeeded).
  uint8_t ccidLastError(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Slot-change notifications from the interrupt IN endpoint. Called on the USB
  // task, so they must not block or issue CCID commands.
  void onCcidCardInserted(CcidSlotChangeCallback callback);
  void onCcidCardRemoved(CcidSlotChangeCallback callback);

  bool midiSend(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendSysEx(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
  // Whether external hubs are tracked. Tracking opens the hub as a client device
  // and keeps the handle, which is what makes hub topology, getHubInfo(),
  // getHubPortStatus() and setHubPortPower() work, and what produces
  // connect/disconnect events for the hub itself. Turning it off makes this library
  // leave external hubs completely alone: devices behind a hub still enumerate and
  // work, but the hub does not appear in getDevices() and the calls above stop
  // working.
  //
  // Note what this does not fix. An external hub is also owned by the ESP-IDF host
  // stack's own hub driver, and one hub/device combination has been seen to crash
  // that driver (a `device_release` assert in ext_hub.c) with this switch off, so
  // holding a client handle is not what provokes it. See the hub notes in
  // tests/manual/README.md.
  //
  // Defaults to on. Set it before begin() to take effect from the first scan.
  void setHubTrackingEnabled(bool enabled);
  bool hubTrackingEnabled() const;
  bool getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change);
  bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
  void networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Raw Ethernet frame transport over an opened USB network interface. Received
  // frames are delivered to onNetworkFrame() (USB task context; keep it light)
  // and also buffered for polling with networkReadFrame(). networkWriteFrame()
  // wraps one Ethernet frame in a single-datagram NCM NTB and sends it.
  bool networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  size_t networkReadFrame(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool networkLinkUp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // lwIP (esp_netif) integration: register the opened USB network interface as
  // a netif so standard Arduino networking (NetworkClient/HTTPClient/ping) runs
  // over the USB NIC. networkAttachNetif() also opens the interface if needed.
  bool networkAttachNetif(const EspUsbHostNetworkConfig &config = EspUsbHostNetworkConfig(),
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool networkDetachNetif(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  IPAddress networkLocalIP(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool networkStats(EspUsbHostNetworkStats &stats, uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardNumLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardCapsLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardScrollLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // True when the attached keyboard reports keys as an NKRO bitmap (report
  // protocol) rather than the 6-key boot report. Detected from the HID report
  // descriptor; decoding is automatic, this is a diagnostic.
  bool keyboardUsesBitmapReport(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t deviceCount() const;
  size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
  bool getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
  size_t getHostDeviceAddresses(uint8_t *addresses, size_t maxAddresses) const;
  bool probeHostDevice(uint8_t address, EspUsbHostDeviceProbeInfo &probe);
  bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
  size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const;
  size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const;
  size_t getNetworkInterfaces(uint8_t address,
                              EspUsbHostNetworkInterfaceInfo *interfaces,
                              size_t maxInterfaces);
  size_t endpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t managedEndpointCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t ep0ChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t hubEndpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t estimatedHcdChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // Host channels the selected controller has. 8 on the ESP32-S2/S3 and on the
  // ESP32-P4's full-speed port, 16 on the P4's high-speed port. One channel goes
  // to each device's EP0, and usb_host_interface_claim() takes one per endpoint
  // of the interface it claims, which is what estimatedHcdChannelCount() adds up.
  size_t maxEndpointChannelCount() const;
  size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams, size_t maxStreams) const;
  // Every format/frame pair a UVC camera on this address advertises, flattened
  // out of the two-level Format/Frame descriptor tree. Pass the result to
  // espUsbHostSelectVideoStream() to choose one.
  //
  // A camera whose configuration descriptor is larger than the core's
  // CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE never enumerates far enough to
  // reach this, and reports zero streams. That limit is 256 bytes up to and
  // including arduino-esp32 3.3.x, which is smaller than almost every real
  // webcam's configuration descriptor; see docs/usb-host-advanced.md.
  size_t getVideoStreams(uint8_t address, EspUsbHostVideoStreamInfo *streams, size_t maxStreams) const;
  // Number of format/frame pairs discovered, without copying any of them.
  size_t getVideoStreamCount(uint8_t address) const;
  // Start streaming one of the formats getVideoStreams() reported.
  //
  // The sequence is the one UVC requires and cannot be shortened: SET_CUR and
  // GET_CUR on the Probe control to find out what the camera will actually
  // send, SET_CUR on the Commit control to fix it, then the alternate setting
  // whose bandwidth covers the dwMaxPayloadTransferSize the camera answered
  // with. Picking the alternate before probing is what makes a naive UVC host
  // produce a stream that never completes a frame.
  //
  // fps is a frame rate, 0 for the format's own default. The frame buffer is
  // allocated here from the format's dwMaxVideoFrameBufferSize and freed by
  // videoStop(), so a large format can fail for memory alone.
  //
  // Must not be called from the USB client task: it waits on control
  // transfers that task is the one to complete.
  bool videoStart(const EspUsbHostVideoStreamInfo &stream,
                  uint32_t fps = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // The same, selecting the format with espUsbHostSelectVideoStream(). Any of
  // format, width, height and fps may be 0 for no preference; a value the
  // camera does not offer fails rather than resolving to something else.
  bool videoStart(uint8_t format,
                  uint16_t width,
                  uint16_t height,
                  uint32_t fps,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Stop streaming, put the interface back on its zero-bandwidth alternate,
  // and free the frame buffer. Safe to call when not streaming.
  bool videoStop(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool videoStreaming(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  // What the camera committed to, which is not always what was asked for.
  bool videoCommitted(EspUsbHostVideoProbeControl &control,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool videoStats(EspUsbHostVideoStats &stats,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

  int lastError() const;
  const char *lastErrorName() const;
  void printDeviceInfo(uint8_t address, bool includeHubInfo = false, Print &out = Serial);
  void printAllDeviceInfo(Print &out = Serial);

private:
  template <typename Callback>
  struct ListenerSlot
  {
    EspUsbHostListenerId id = ESP_USB_HOST_INVALID_LISTENER_ID;
    std::shared_ptr<Callback> callback;
  };

  template <typename Callback, size_t Capacity = ESP_USB_HOST_MAX_LISTENERS_PER_EVENT>
  struct ListenerRegistry
  {
    ListenerSlot<Callback> slots[Capacity];
    size_t count = 0;
  };

  struct EndpointState
  {
    bool inUse = false;
    uint8_t deviceIndex = 0xff;
    uint8_t deviceAddress = 0;
    usb_device_handle_t deviceHandle = nullptr;
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint8_t alternate = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    uint8_t audioChannels = 0;
    uint8_t audioBytesPerSample = 0;
    uint8_t audioBitsPerSample = 0;
    usb_transfer_t *transfer = nullptr;
    bool transferSubmitted = false;
    // Set while an endpoint is being taken over or torn down: no path may submit
    // a new transfer on it, so the outstanding one can be canceled and freed.
    bool stopping = false;
    bool recoveryPending = false;
    bool resubmitPending = false;
    bool resubmitAfterLed = false;
    uint8_t lastKeyboardReport[8] = {};
    bool keyboardReportReady = false;
    uint8_t lastKeyboardState[ESP_USB_HOST_KEYBOARD_BITMAP_SIZE] = {};
    // Previous NKRO bitmap report state, for press/release diffing.
    uint8_t lastKeyboardBitmap[ESP_USB_HOST_NKRO_BITMAP_MAX_BYTES] = {};
    uint8_t lastKeyboardBitmapModifiers = 0;
    bool keyboardBitmapReady = false;
    uint8_t lastMouseButtons = 0;
    uint16_t lastMouseButtonMask = 0;
    uint16_t lastConsumerUsage = 0;
    EspUsbHostGamepadPrevState lastGamepadState;
    // Generic HID field values are only needed by decoded gamepad events.
    EspUsbHostHIDFieldValue *hidFieldValues = nullptr;
    size_t hidFieldValueCount = 0;
    uint8_t lastSystemUsage = 0;
  };

  struct HIDInputFieldState
  {
    uint8_t interfaceNumber = 0;
    uint8_t reportId = 0;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint16_t bitOffset = 0;
    uint8_t bitSize = 0;
    uint8_t flags = 0;
  };

  struct HIDReportDescriptorState
  {
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint16_t hidVersion = 0;
    uint8_t countryCode = 0;
    uint8_t descriptorType = USB_HID_REPORT_DESC;
    uint16_t reportedLength = 0;
  };

  // UAC2 Clock Source entity. bmControls D1..D0 tell whether the sample frequency
  // control is present and programmable, which decides whether a rate change can
  // be pushed to the device or only read back.
  // One alternate setting of a VideoStreaming interface. payloadSize is the
  // bandwidth it offers per microframe, already multiplied out by
  // espUsbHostVideoIsocPayloadSize(), which is the number the alternate has to
  // be chosen by.
  struct VideoAlternateState
  {
    uint8_t interfaceNumber = 0xff;
    uint8_t alternate = 0;
    uint8_t endpointAddress = 0;
    // What the descriptor offers per service interval, transactions multiplied
    // out. Reported as-is so a dump shows what the camera asked for.
    uint32_t payloadSize = 0;
    // What this host can actually take per interval. Equal to payloadSize unless
    // the alternate is high-bandwidth and the controller cannot run more than one
    // transaction, in which case it is the bare packet size. Selection and
    // transfer sizing use this one; using payloadSize there would size transfers
    // for bandwidth that never arrives.
    uint32_t usablePayload = 0;
    // 1, 2 or 3.
    uint8_t transactions = 1;
    uint8_t interval = 0;
    bool isochronous = true;
  };

  // Per-device USB Video state. Allocated by videoStateFor(device, true) when a
  // VideoControl or VideoStreaming descriptor is first seen, and freed with the
  // device.
  struct VideoState
  {
    // bcdUVC from the VideoControl header: 0x0100, 0x0110 or 0x0150. It decides
    // the Probe/Commit control length, which a camera stalls if it is wrong.
    uint16_t version = 0;
    uint8_t controlInterface = 0xff;
    uint8_t streamingInterface = 0xff;
    // bEndpointAddress from VS_INPUT_HEADER: which endpoint the camera will
    // stream on once an alternate carrying it is selected.
    uint8_t streamingEndpoint = 0;
    EspUsbHostVideoStreamInfo streamInfos[ESP_USB_HOST_MAX_VIDEO_STREAMS] = {};
    uint8_t streamInfoCount = 0;
    VideoAlternateState alternates[ESP_USB_HOST_MAX_VIDEO_ALTERNATES] = {};
    uint8_t alternateCount = 0;
    bool streamingActive = false;
    // Set while the streaming transfers are being torn down, so a completion
    // callback that is already running does not resubmit.
    bool stopping = false;
    usb_transfer_t *transfers[ESP_USB_HOST_MAX_VIDEO_TRANSFERS] = {};
    bool transferInFlight[ESP_USB_HOST_MAX_VIDEO_TRANSFERS] = {};
    uint8_t transferCount = 0;
    uint8_t activeAlternate = 0;
    uint8_t activeEndpoint = 0;
    // Bytes requested per isochronous service interval on the active stream. Kept
    // separately from the stream's maxPayloadSize, which reports what the camera
    // offered rather than what this host asked for.
    uint32_t activePacketBytes = 0;
    EspUsbHostVideoStreamInfo activeStream;
    EspUsbHostVideoProbeControl commit;
    // Frame being assembled. The buffer is allocated by videoStart() from the
    // format's dwMaxVideoFrameBufferSize and freed by videoStop().
    uint8_t *frameBuffer = nullptr;
    size_t frameCapacity = 0;
    size_t frameLength = 0;
    bool frameOpen = false;
    // Set when something went wrong inside the frame currently being assembled,
    // so it is still delivered but marked incomplete.
    bool frameBad = false;
    // The payload header frame ID toggles at every frame boundary. It is the only
    // boundary marker left when an end-of-frame payload is lost.
    bool frameIdValid = false;
    bool frameId = false;
    uint32_t frameSequence = 0;
    bool frameHasPts = false;
    uint32_t framePts = 0;
    EspUsbHostVideoStats stats;
  };

  struct AudioClockSourceState
  {
    uint8_t clockSourceId = 0;
    uint8_t attributes = 0;
    uint8_t controls = 0;
  };

  // Input/Output Terminal to Clock Source link (bCSourceID), used to resolve a
  // streaming interface's clock through its bTerminalLink.
  struct AudioTerminalClockLink
  {
    uint8_t terminalId = 0;
    uint8_t clockSourceId = 0;
  };

  // Per-device USB Audio state, allocated only when the device's descriptors
  // say it has an Audio interface. A pointer rather than the state itself, for
  // the same reason as VideoState: inline it cost about 740 bytes in every one
  // of the ESP_USB_HOST_MAX_DEVICES slots, paid by every sketch whether or not
  // an audio device was ever plugged in. Null means "no Audio interface", so
  // there is no separate flag to keep in step with it.

  // Per-device vendor bulk state, allocated by vendorOpen() and freed with the
  // device. Unlike audio and video, which are discovered during enumeration,
  // this has a single creation point: a sketch asks for the interface. Inline it
  // cost about 720 bytes in every device slot, over 500 of which is the receive
  // ring alone, paid by every sketch that never calls vendorOpen().
  struct UsbVendorState
  {
    bool hasInterface = false;
    uint8_t interfaceNumber = 0xff;
    bool readOnDemand = false;
    bool hasInEndpoint = false;
    uint8_t inEndpointAddress = 0;
    uint16_t inPacketSize = 0;
    size_t inTransferBytes = 0;
    bool hasOutEndpoint = false;
    uint8_t outEndpointAddress = 0;
    uint16_t outPacketSize = 0;
    uint8_t rxBuffer[ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE] = {};
    size_t rxHead = 0;
    size_t rxTail = 0;
    size_t rxCount = 0;
    portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
    bool outQueueActive = false;
    uint8_t outQueueDepth = 0;
    size_t outBufferBytes = 0;
    usb_transfer_t *outTransfers[ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH] = {};
    uint8_t outSlotState[ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH] = {};
    SemaphoreHandle_t outFreeSlots = nullptr;
    bool outHalted = false;
    bool autoZlp = false;
    EspUsbHostVendorWriteStats writeStats;
    bool inQueueActive = false;
    uint8_t inQueueDepth = 0;
    size_t inBufferBytes = 0;
    usb_transfer_t *inTransfers[ESP_USB_HOST_VENDOR_READ_QUEUE_MAX_DEPTH] = {};
    bool inSlotInFlight[ESP_USB_HOST_VENDOR_READ_QUEUE_MAX_DEPTH] = {};
    bool inHalted = false;
    bool inRefillPending = false;
    EspUsbHostVendorReadStats readStats;
  };
  struct AudioState
  {
    bool hasInterface = false;
    uint8_t interfaceNumber = 0;
    bool hasInEndpoint = false;
    uint8_t inInterfaceNumber = 0;
    uint8_t inAlternate = 0;
    uint8_t inEndpointAddress = 0;
    uint8_t inChannels = 0;
    uint8_t inBytesPerSample = 0;
    uint8_t inBitsPerSample = 0;
    bool hasOutEndpoint = false;
    uint8_t outInterfaceNumber = 0;
    uint8_t outEndpointAddress = 0;
    uint16_t outPacketSize = 0;
    uint8_t outChannels = 0;
    uint8_t outBytesPerSample = 0;
    uint8_t outBitsPerSample = 0;
    uint8_t outInterval = 0;
    bool outRunning = false;
    uint32_t outFrameAccumulator = 0;
    uint32_t outUnderruns = 0;
    usb_transfer_t *outTransfers[ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS] = {};
    uint8_t outFeedbackInterfaceNumber = 0xff;
    uint8_t outFeedbackEndpointAddress = 0;
    uint16_t outFeedbackPacketSize = 0;
    uint8_t outFeedbackInterval = 0;
    usb_transfer_t *outFeedbackTransfer = nullptr;
    uint32_t outFeedbackRate = 0;
    uint32_t outFeedbackUpdates = 0;
    uint32_t outFeedbackRejects = 0;
    uint32_t sampleRate = 48000;
    uint8_t controlInterfaceNumber = 0xff;
    uint8_t protocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
    EspUsbHostAudioFeatureUnitInfo featureUnits[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS] = {};
    uint8_t featureUnitCount = 0;
    AudioClockSourceState clockSources[ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES] = {};
    uint8_t clockSourceCount = 0;
    AudioTerminalClockLink terminalClocks[ESP_USB_HOST_MAX_AUDIO_TERMINALS] = {};
    uint8_t terminalClockCount = 0;
    EspUsbHostAudioStreamInfo streamInfos[ESP_USB_HOST_MAX_AUDIO_STREAMS] = {};
    uint8_t streamInfoCount = 0;
  };

  // One CDC-ACM function (control + data interface pair), or the single VCP of a
  // vendor USB-serial bridge. Everything a serial port owns lives here rather
  // than on DeviceState, so a composite device with two ACM functions keeps two
  // independent line codings, OUT endpoints and write queues instead of the
  // second function overwriting the first.
  // Asynchronous CDC OUT queue for one port, allocated by serialWriteQueueBegin()
  // and released when the queue ends or the device goes away. It is roughly three
  // times the size of the rest of a port, and most ports never open one, so
  // keeping it off SerialPortState is what lets a device carry as many ports as
  // the controller has channels for without paying for queues nobody asked for.
  // serialWriteQueueBegin() already allocates a transfer pool and a semaphore, so
  // this rides along with allocations that were there.
  struct SerialOutQueue
  {
    // Cleared by serialWriteQueueEnd() before draining, so pending() can reach
    // zero while the pool is still being handed back.
    bool active = false;
    uint8_t depth = 0;
    size_t bufferBytes = 0;
    usb_transfer_t *transfers[ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH] = {};
    uint8_t slotState[ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH] = {};
    SemaphoreHandle_t freeSlots = nullptr;
    bool halted = false;
    EspUsbHostSerialWriteStats stats;
  };

  struct SerialPortState
  {
    bool inUse = false;
    bool hasControlInterface = false;
    bool hasDataInterface = false;
    // SET_LINE_CODING / SET_CONTROL_LINE_STATE have been sent for this port.
    bool configured = false;
    // Driven through a vendor protocol (CH340 / CP210x / FTDI / PL2303) rather
    // than CDC-ACM class requests.
    bool vendorSerial = false;
    uint8_t controlInterfaceNumber = 0xff;
    // Learned from the CDC Union functional descriptor when the device provides
    // one, otherwise from the next CDC-DATA interface after the control one.
    uint8_t dataInterfaceNumber = 0xff;
    bool hasInEndpoint = false;
    uint8_t inEndpointAddress = 0;
    bool hasOutEndpoint = false;
    uint8_t outEndpointAddress = 0;
    uint16_t outPacketSize = 0;
    EspUsbHostSerialConfig config;
    bool dtr = true;
    bool rts = true;
    // nullptr until serialWriteQueueBegin() opens a queue on this port.
    SerialOutQueue *outQueue = nullptr;
  };

  struct DeviceState
  {
    bool inUse = false;
    usb_device_handle_t handle = nullptr;
    EspUsbHostDeviceInfo info;
    String manufacturer;
    String product;
    String serial;
    bool hasKeyboardInterface = false;
    uint8_t keyboardInterfaceNumber = 0;
    // Keyboard input-report layout learned from the HID report descriptor. When a
    // device reports keys as an NKRO bitmap (report protocol) instead of the 8-byte
    // boot report, keyboardBitmapReport is true and the offsets below locate the
    // modifier byte and the key bitmap within the report body.
    bool keyboardBitmapReport = false;
    uint8_t keyboardLayoutInterface = 0xff; // interface the layout below describes
    uint8_t keyboardLayoutReportId = 0;     // report ID prefix (0 = none)
    bool keyboardHasModifierField = false;
    uint16_t keyboardModifierBitOffset = 0;
    uint16_t keyboardBitmapBitOffset = 0;
    uint16_t keyboardBitmapBitCount = 0;
    uint16_t keyboardBitmapUsageMin = 0;
    // Mouse input-report layout learned from the HID report descriptor. A device
    // is in report protocol after enumeration, so the report only matches the
    // 4-byte boot layout by coincidence; mice that declare more than 8 buttons
    // or 16-bit axes need their fields located from the descriptor.
    EspUsbHostMouseReportLayout mouseLayout;
    uint8_t mouseLayoutInterface = 0xff;
    // Keyboard LED output report learned from the HID report descriptor (LED usage
    // page in an Output item). Lets setKeyboardLeds() reach keyboards that never
    // declare a boot interface (report-ID composites, NKRO keyboards): the LED
    // Set_Report then targets this interface with this report ID instead of the
    // boot interface with report ID 0.
    bool hasKeyboardLedOutput = false;
    uint8_t keyboardLedInterface = 0xff;
    uint8_t keyboardLedReportId = 0;
    bool keyboardNumLock = true;
    bool keyboardCapsLock = false;
    bool keyboardScrollLock = false;
    bool keyboardLedPending = false;
    bool keyboardLedDirty = false;
    uint32_t keyboardLedDirtyTimeMs = 0;
    uint8_t keyboardLedLastSent = 0;
    bool hasVendorInterface = false;
    uint8_t vendorInterfaceNumber = 0;
    bool hasVendorOutEndpoint = false;
    uint8_t vendorOutEndpointAddress = 0;
    uint16_t vendorOutPacketSize = 0;
    // CDC-ACM / VCP ports, filled in descriptor order. serialPortCount counts the
    // slots actually populated; ports the device published beyond
    // ESP_USB_HOST_MAX_SERIAL_PORTS are left unclaimed.
    SerialPortState serialPorts[ESP_USB_HOST_MAX_SERIAL_PORTS];
    uint8_t serialPortCount = 0;
    // Line coding a sketch chose before the device enumerated, applied to every
    // port of this device as its control interface is claimed.
    EspUsbHostSerialConfig serialConfig;
    bool hasVendorSerialInterface = false;
    bool vendorSerialSupported = false;
    uint8_t vendorSerialInterfaceNumber = 0;
    // Bytes per continuous IN transfer, rounded up to a whole number of packets
    // by vendorOpen(). 0 until a continuous transfer is set up.
    // The ring is filled from the USB client task and drained by whichever task
    // calls vendorRead(), and it is not a plain single-producer/single-consumer
    // ring: when it overflows the producer advances the *consumer's* tail to
    // discard the oldest bytes. That breaks the rule that makes a lock-free ring
    // safe -- each index written by one side only -- so both sides take this.
    // Without it a vendorRead() in progress can have the tail moved out from
    // under it and return a window with a seam in the middle of a message.
    // Asynchronous bulk OUT queue. Slots are preallocated by
    // vendorWriteQueueBegin() and reused; usbVendorOutFreeSlots counts the slots
    // that are neither acquired nor in flight.
    // Asynchronous bulk IN queue. Slots are preallocated by
    // vendorReadQueueBegin() and resubmitted from their own completion callback,
    // so the endpoint keeps several transfers outstanding.
    // A stalled pipe and a failed resubmit are both repaired from the client
    // task, which owns endpoint recovery; the callback only records them.
    bool hasMidiInterface = false;
    uint8_t midiInterfaceNumber = 0;
    bool hasMidiOutEndpoint = false;
    uint8_t midiOutEndpointAddress = 0;
    uint16_t midiOutPacketSize = 0;
    uint8_t midiInCableCount = 0;
    uint8_t midiOutCableCount = 0;
    // Explicit feedback endpoint of an asynchronous playback interface, when the
    // claimed alternate declares one. audioOutFeedbackRate is the last plausible
    // rate the device asked for; audioOutRate() falls back to the negotiated rate
    // while it is 0, so a synchronous device behaves exactly as before.
    // bInterfaceProtocol of the device's Audio interfaces (0x20 for UAC2), taken
    // from the Audio Control interface and reused for its streaming interfaces.
    // UAC2 clock topology. Clock Source entities carry the sample frequency
    // control, and the Input/Output Terminal a streaming interface links to names
    // the clock that drives it.
    bool hasMscInterface = false;
    uint8_t mscInterfaceNumber = 0;
    bool hasMscInEndpoint = false;
    uint8_t mscInEndpointAddress = 0;
    uint16_t mscInPacketSize = 0;
    bool hasMscOutEndpoint = false;
    uint8_t mscOutEndpointAddress = 0;
    uint16_t mscOutPacketSize = 0;
    uint32_t mscTag = 1;
    uint32_t mscBlockCount = 0;
    uint64_t mscBlockCount64 = 0;
    uint32_t mscBlockSize = 0;
    EspUsbHostMscSense mscLastSense = {};
    bool hasMscLastSense = false;
    uint8_t mscMaxLun = 0;
    bool hasMscMaxLun = false;
    uint8_t mscLun = 0;
    // Latched when SYNCHRONIZE CACHE(10) fails once, so later calls skip the
    // command instead of stalling the bulk pipes again on the same device.
    bool mscSyncCacheUnsupported = false;
    // CCID class descriptor values, filled during enumeration (before any
    // ccidOpen()) so the reader's limits are known when the interface is opened.
    bool ccidHasClassDescriptor = false;
    uint8_t ccidDescriptorInterfaceNumber = 0xff;
    uint16_t ccidBcd = 0;
    uint8_t ccidSlotCount = 1;
    uint8_t ccidVoltageSupport = 0;
    uint32_t ccidProtocols = 0;
    uint32_t ccidFeatures = 0;
    uint32_t ccidMaxMessageLength = 0;
    uint8_t ccidMaxBusySlots = 1;
    // Set by ccidOpen(), cleared by ccidClose() and on disconnect.
    bool hasCcidInterface = false;
    uint8_t ccidInterfaceNumber = 0xff;
    uint8_t ccidInEndpointAddress = 0;
    uint16_t ccidInPacketSize = 0;
    uint8_t ccidOutEndpointAddress = 0;
    uint16_t ccidOutPacketSize = 0;
    uint8_t ccidInterruptEndpointAddress = 0;
    uint16_t ccidInterruptPacketSize = 0;
    uint8_t ccidSequence = 0;
    uint8_t ccidError = 0;
    // Response reassembly buffer. Only allocated while a CCID interface is open.
    uint8_t *ccidBuffer = nullptr;
    size_t ccidBufferSize = 0;
    size_t ccidResponseLength = 0;
    uint8_t ccidAtr[ESP_USB_HOST_CCID_MAX_ATR] = {};
    uint8_t ccidAtrLength = 0;
    uint8_t ccidAtrSlot = 0;
    // One bit per slot; ccidSlotKnownMask says which bits the reader reported.
    uint8_t ccidSlotPresentMask = 0;
    uint8_t ccidSlotKnownMask = 0;
    // Serializes CCID commands so two callers cannot interleave bSeq values.
    SemaphoreHandle_t ccidLock = nullptr;
    bool hasNetworkInterface = false;
    EspUsbHostNetworkInterfaceInfo networkInterface;
    bool networkLinkUp = false;
    uint16_t networkTxSequence = 0;
    // Reusable bulk-OUT transfer + its completion semaphore, so networkWriteFrame()
    // does not alloc/free a transfer and a semaphore on every frame. networkTxLock
    // serializes concurrent senders (a user thread and the lwIP transmit hook) so
    // they cannot corrupt the shared transfer / sequence counter, and lets teardown
    // drain an in-flight send before freeing. The lock and completion semaphore are
    // created once per device slot and preserved across resetDeviceState() (never
    // deleted) so a concurrent sender can never block on or signal a freed handle.
    usb_transfer_t *networkOutTransfer = nullptr;
    SemaphoreHandle_t networkOutDone = nullptr;
    SemaphoreHandle_t networkTxLock = nullptr;
    // Allocated only while a network interface is open. Keeping these large
    // buffers out of every DeviceState slot avoids reserving ~7 KB per tracked
    // device for sketches that never use USB networking.
    uint8_t *networkRxRing = nullptr;
    volatile uint16_t networkRxHead = 0;
    volatile uint16_t networkRxTail = 0;
    void *networkNetif = nullptr; // esp_netif_t* (opaque here to keep esp_netif out of the header)
    bool networkNetifAttached = false;
    uint32_t networkRxNtbCount = 0;
    uint32_t networkRxFrameCount = 0;
    uint32_t networkTxCount = 0;
    uint32_t networkTxFailCount = 0;
    uint32_t networkRxOversizedCount = 0;
    // Negotiated device->host NTB limit: the size of networkAsm, the length the
    // bulk-IN transfer is submitted with, and the value handed to the device via
    // SET_NTB_INPUT_SIZE. Always a multiple of the IN endpoint's max packet size.
    uint16_t networkNtbInSize = 0;
    // dwNtbOutMaxSize from GET_NTB_PARAMETERS: the largest host->device NTB the
    // device accepts. 0 when the device did not answer the request.
    uint16_t networkNtbOutMax = 0;
    // Reassembly buffer: a device->host NTB can span several bulk-IN completions
    // (one per USB packet at full speed), so accumulate until wBlockLength bytes.
    uint8_t *networkAsm = nullptr;
    uint16_t networkAsmLen = 0;
    uint16_t networkAsmExpected = 0;
    // Everything this device needs for USB Video, allocated only when its
    // descriptors say it is a camera. A pointer rather than the state itself:
    // inline it cost about 1 KB in every one of the ESP_USB_HOST_MAX_DEVICES
    // slots -- 8 KB of static RAM on an ESP32-P4 -- paid by every sketch whether
    // or not a camera was ever plugged in. Null means "not a camera", so there is
    // no separate flag to keep in step with it.
    UsbVendorState *usbVendor = nullptr;
    AudioState *audio = nullptr;
    VideoState *video = nullptr;
    EspUsbHostInterfaceInfo interfaceInfos[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceInfoCount = 0;
    EspUsbHostEndpointInfo endpointInfos[ESP_USB_HOST_MAX_ENDPOINTS] = {};
    uint8_t endpointInfoCount = 0;
    uint8_t endpointChannelCount = 0;
    HIDReportDescriptorState hidReportDescriptors[ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS] = {};
    uint8_t hidReportDescriptorCount = 0;
    // Allocated when a HID report descriptor is first parsed. Most device
    // slots never need this comparatively large field table.
    HIDInputFieldState *hidInputFields = nullptr;
    size_t hidInputFieldCount = 0;
    uint8_t interfaces[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceCount = 0;
    bool isHub = false;
    uint8_t hubIndex = 0;
    bool disconnectPending = false;
  };

  static void taskEntry(void *arg);
  static void clientTaskEntry(void *arg);
  static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  static void transferCallback(usb_transfer_t *transfer);
  static void controlTransferCallback(usb_transfer_t *transfer);
  static void hidReportDescriptorTransferCallback(usb_transfer_t *transfer);
  static void outputTransferCallback(usb_transfer_t *transfer);
  static void serialOutTransferCallback(usb_transfer_t *transfer);
  static void vendorOutTransferCallback(usb_transfer_t *transfer);
  static void vendorInTransferCallback(usb_transfer_t *transfer);

  void taskLoop();
  void clientTaskLoop();
  void handleClientEvent(const usb_host_client_event_msg_t *eventMsg);
  void handleNewDevice(uint8_t address);
  void handleDeviceGone(usb_device_handle_t goneHandle);
  void scanHostDevices();
  void refreshDeviceTopology(DeviceState &device);
  void parseConfigDescriptor(DeviceState &device, const usb_config_desc_t *configDesc);
  size_t parseNetworkInterfaces(uint8_t address,
                                const usb_config_desc_t *configDesc,
                                EspUsbHostNetworkInterfaceInfo *interfaces,
                                size_t maxInterfaces) const;
  void handleDescriptor(uint8_t descriptorType, const uint8_t *data);
  void parseAudioControlDescriptor(DeviceState &device, const uint8_t *data);
  void parseAudioFeatureUnitDescriptor(DeviceState &device, const uint8_t *data);
  void parseAudioClockSourceDescriptor(DeviceState &device, const uint8_t *data);
  void parseAudioTerminalDescriptor(DeviceState &device, const uint8_t *data, bool input);
  void parseAudioStreamingDescriptor(DeviceState &device, const uint8_t *data);
  void parseVideoControlDescriptor(DeviceState &device, const uint8_t *data);
  void parseVideoStreamingDescriptor(DeviceState &device, const uint8_t *data);
  // Commits the format/frame pair the scan just finished reading. Called once
  // per Frame descriptor, because each Frame under a Format is a separate
  // stream from a caller's point of view.
  void recordVideoStream(DeviceState &device, const EspUsbHostVideoStreamInfo &stream);
  // The device's video state, allocating it on first use when create is true.
  // Returns null when the device has none, or when the allocation failed -- in
  // which case the device simply enumerates without video rather than the
  // whole enumeration failing.
  UsbVendorState *usbVendorStateFor(DeviceState &device, bool create = false);
  const UsbVendorState *usbVendorStateFor(const DeviceState &device) const;
  void releaseUsbVendorState(DeviceState &device);
  AudioState *audioStateFor(DeviceState &device, bool create = false);
  const AudioState *audioStateFor(const DeviceState &device) const;
  void releaseAudioState(DeviceState &device);
  VideoState *videoStateFor(DeviceState &device, bool create = false);
  const VideoState *videoStateFor(const DeviceState &device) const;
  void releaseVideoState(DeviceState &device);
  void recordVideoAlternate(DeviceState &device, const usb_ep_desc_t *ep, bool isochronous);
  // SET_INTERFACE, waited for rather than fired and forgotten.
  bool setInterfaceSync(DeviceState &device,
                        uint8_t interfaceNumber,
                        uint8_t alternateSetting,
                        uint32_t timeoutMs);
  // One Probe or Commit control request. control is the selector
  // (VS_PROBE_CONTROL / VS_COMMIT_CONTROL), request the class request code.
  bool videoStreamingControl(DeviceState &device,
                             uint8_t request,
                             uint8_t control,
                             uint8_t *data,
                             size_t length,
                             bool dataIn,
                             uint32_t timeoutMs);
  // GET_CUR on the Stream Error Code control: why the camera stalled the last
  // request. Returns 0 when the camera does not implement it.
  uint8_t videoStreamErrorCode(DeviceState &device);
  // The full Probe/Commit exchange. Fills committed with what the camera
  // answered, which is the authority on payload size and frame size.
  bool videoNegotiate(DeviceState &device,
                      const EspUsbHostVideoStreamInfo &stream,
                      uint32_t frameInterval,
                      EspUsbHostVideoProbeControl &committed);
  // Smallest alternate setting whose payload size covers payloadBytes, or the
  // largest available when none does. Smallest rather than largest because an
  // isochronous alternate reserves its bandwidth for as long as it is
  // selected, whether or not the camera fills it.
  const VideoAlternateState *selectVideoAlternate(const DeviceState &device,
                                                  uint8_t interfaceNumber,
                                                  uint32_t payloadBytes) const;
  static void videoTransferCallback(usb_transfer_t *transfer);
  int videoSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const;
  bool submitVideoTransfer(DeviceState &device, uint8_t slot);
  void releaseVideoTransfers(DeviceState &device, bool devicePresent);
  void handleVideo(DeviceState &device, usb_transfer_t *transfer);
  // Appends one payload to the frame being assembled, opening and closing
  // frames as the payload headers say to.
  void videoAppendPayload(DeviceState &device,
                          const EspUsbHostVideoPayloadHeader &header,
                          const uint8_t *data,
                          size_t length);
  void videoDeliverFrame(DeviceState &device);
  void videoResetAssembly(DeviceState &device);
  // Tears down the streaming endpoint, interface claim and buffer. Used by
  // videoStop() and by the disconnect path, so it must tolerate a device that
  // is already gone.
  void releaseVideoStreaming(DeviceState &device, bool devicePresent);
  // Marks every discovered format startable or not once the whole
  // configuration has been walked, which is the first point at which the
  // available alternate settings are known.
  void finalizeVideoStreams(DeviceState &device);
  // Clock Source entity that drives a streaming interface, resolved through the
  // interface's bTerminalLink. Falls back to the only declared clock source when
  // the terminal link cannot be matched, and returns 0 when there is none.
  uint8_t resolveAudioClockSource(const DeviceState &device, uint8_t terminalLink) const;
  const AudioClockSourceState *findAudioClockSource(const DeviceState &device, uint8_t clockSourceId) const;
  // startable is false for an alternate setting that was parsed but not claimed:
  // its format is reported, but no endpoint or transfer is allocated for it.
  void recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input, bool startable = true);
  void handleTransfer(usb_transfer_t *transfer);
  void dispatchKeyboardState(EndpointState &endpoint,
                             DeviceState *device,
                             const uint8_t *bitmap,
                             const uint8_t *rawData,
                             size_t rawLength,
                             const uint8_t *reportData,
                             size_t reportLength);
  void handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleKeyboardBitmap(EndpointState &endpoint, DeviceState &device, const uint8_t *data, size_t length);
  void handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMidi(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleAudio(EndpointState &endpoint, usb_transfer_t *transfer);
  void handleUsbVendorData(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleHIDVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void parseHIDReportDescriptor(DeviceState &device, const EspUsbHostHIDReportDescriptor &descriptor);
  bool hasHIDReportId(const DeviceState &device, uint8_t interfaceNumber, uint8_t reportId) const;
  size_t decodeHIDInputFields(const DeviceState &device,
                              uint8_t interfaceNumber,
                              uint8_t reportId,
                              const uint8_t *data,
                              size_t length,
                              EspUsbHostHIDFieldValue *fields,
                              size_t maxFields) const;

  EndpointState *findEndpoint(usb_device_handle_t deviceHandle, uint8_t endpointAddress);
  EndpointState *allocateEndpoint(DeviceState &device);
  DeviceState *allocateDevice();
  void resetDeviceState(DeviceState &device);
  void resetEndpointState(EndpointState &endpoint);
  DeviceState *findDevice(uint8_t address);
  const DeviceState *findDevice(uint8_t address) const;
  DeviceState *findDeviceByHandle(usb_device_handle_t handle);
  DeviceState *findSerialDevice(uint8_t address, uint8_t port = ESP_USB_HOST_ANY_PORT);
  const DeviceState *findSerialDevice(uint8_t address, uint8_t port = ESP_USB_HOST_ANY_PORT) const;
  // Resolves (address, port) to one port's state, optionally handing back the
  // device that owns it. ESP_USB_HOST_ANY_PORT picks the device's first ready
  // port; a device with no ready port is skipped when the address is
  // ESP_USB_HOST_ANY_ADDRESS, exactly as findSerialDevice() always behaved.
  SerialPortState *findSerialPort(uint8_t address, uint8_t port, DeviceState **deviceOut = nullptr);
  const SerialPortState *findSerialPort(uint8_t address, uint8_t port, const DeviceState **deviceOut = nullptr) const;
  // Port slot for a newly seen CDC control interface, or nullptr once
  // ESP_USB_HOST_MAX_SERIAL_PORTS slots are taken.
  SerialPortState *allocateSerialPort(DeviceState &device);
  // Port that owns an interface number as its data (or vendor VCP) interface.
  SerialPortState *serialPortForDataInterface(DeviceState &device, uint8_t interfaceNumber);
  SerialPortState *serialPortForControlInterface(DeviceState &device, uint8_t interfaceNumber);
  // The port still waiting for its data interface: the one whose Union functional
  // descriptor named this interface, else the most recent claimed control
  // interface that has no data interface yet (devices that omit the Union pair
  // their interfaces by descriptor order).
  SerialPortState *pendingSerialPort(DeviceState &device, uint8_t interfaceNumber);
  // Port that owns a claimed IN or OUT endpoint address.
  SerialPortState *serialPortForEndpoint(DeviceState &device, uint8_t endpointAddress);
  uint8_t serialPortIndex(const DeviceState &device, const SerialPortState &port) const;
  DeviceState *findMidiDevice(uint8_t address);
  const DeviceState *findMidiDevice(uint8_t address) const;
  DeviceState *findAudioOutputDevice(uint8_t address);
  const DeviceState *findAudioOutputDevice(uint8_t address) const;
  DeviceState *findAudioInputDevice(uint8_t address);
  const DeviceState *findAudioInputDevice(uint8_t address) const;
  const DeviceState *findAudioDevice(uint8_t address) const;
  DeviceState *findVideoDevice(uint8_t address);
  const DeviceState *findVideoDevice(uint8_t address) const;
  DeviceState *findAudioControlDevice(uint8_t address);
  const DeviceState *findAudioControlDevice(uint8_t address) const;
  const EspUsbHostAudioFeatureUnitInfo *findAudioFeatureUnit(const DeviceState &device,
                                                             uint8_t unitId,
                                                             uint8_t controlSelector,
                                                             uint8_t channel) const;
  const EspUsbHostAudioFeatureUnitInfo *findAudioPlaybackFeatureUnit(const DeviceState &device,
                                                                     uint8_t unitId,
                                                                     uint8_t channel) const;
  DeviceState *findMscDevice(uint8_t address);
  const DeviceState *findMscDevice(uint8_t address) const;
  DeviceState *findKeyboardDevice(uint8_t address);
  const DeviceState *findKeyboardDevice(uint8_t address) const;
  DeviceState *findHIDVendorDevice(uint8_t address);
  DeviceState *findUsbVendorDevice(uint8_t address);
  const DeviceState *findUsbVendorDevice(uint8_t address) const;
  DeviceState *findUsbVendorCandidate(uint8_t address, uint8_t interfaceNumber);
  DeviceState *findCcidDevice(uint8_t address);
  const DeviceState *findCcidDevice(uint8_t address) const;
  DeviceState *findCcidCandidate(uint8_t address, uint8_t interfaceNumber);
  void parseCcidClassDescriptor(DeviceState &device, const uint8_t *data);
  void releaseCcidInterface(DeviceState &device);
  void handleCcidNotification(DeviceState &device, const uint8_t *data, size_t length);
  // One PC_to_RDR message plus its RDR_to_PC response, with bSeq matching and
  // time-extension waits. Callers must already hold device.ccidLock.
  bool ccidExchange(DeviceState &device,
                    uint8_t messageType,
                    uint8_t slot,
                    const uint8_t messageSpecific[3],
                    const uint8_t *data,
                    size_t length,
                    EspUsbHostCcidResponse &response,
                    uint32_t timeoutMs);
  bool ccidBulkOut(DeviceState &device, const uint8_t *data, size_t length, uint32_t timeoutMs);
  bool ccidBulkIn(DeviceState &device, uint32_t timeoutMs);
  bool ccidDataExchange(uint8_t messageType,
                        const uint8_t *tx,
                        size_t txLength,
                        uint8_t *rx,
                        size_t rxCapacity,
                        size_t *rxLength,
                        uint8_t slot,
                        uint8_t address,
                        uint32_t timeoutMs);
  bool vendorInterfaceEligible(const DeviceState &device,
                               const EspUsbHostInterfaceInfo &intf,
                               uint8_t interfaceNumber) const;
  static size_t vendorReadTransferBytes(uint16_t packetSize, size_t requested);
  void vendorRxPush(DeviceState &device, const uint8_t *data, size_t length);
  int serialOutSlotOf(const SerialOutQueue &queue, const uint8_t *buffer) const;
  int serialOutSlotOfTransfer(const SerialOutQueue &queue, const usb_transfer_t *transfer) const;
  bool submitSerialOutSlot(DeviceState &device, SerialPortState &port, int slot, size_t length);
  void releaseSerialOutQueue(SerialPortState &port);
  void releaseSerialOutQueues(DeviceState &device);
  void serialDrainOut(SerialPortState &port);
  void serialDrainOutAll(DeviceState &device);
  int vendorInSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const;
  bool submitVendorInSlot(DeviceState &device, uint8_t slot);
  bool stopVendorContinuousIn(DeviceState &device);
  void serviceVendorInQueue(DeviceState &device);
  void releaseVendorInQueue(DeviceState &device);
  void vendorDrainIn(DeviceState &device);
  size_t vendorInInFlight(const DeviceState &device) const;
  void dispatchVendorData(DeviceState &device,
                          uint8_t interfaceNumber,
                          uint8_t endpointAddress,
                          const uint8_t *data,
                          size_t length);
  int vendorOutSlotOf(const DeviceState &device, const uint8_t *buffer) const;
  int vendorOutSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const;
  bool submitVendorOutSlot(DeviceState &device, int slot, size_t length);
  bool submitVendorOutZlp(DeviceState &device);
  void releaseVendorOutQueue(DeviceState &device);
  void vendorDrainOut(DeviceState &device);
  DeviceState *findNetworkDevice(uint8_t address);
  const DeviceState *findNetworkDevice(uint8_t address) const;
  void releaseEndpoints(DeviceState &device, bool clearEndpoints);
  void releaseAllEndpoints(bool clearEndpoints);
  void releaseInterfaces(DeviceState &device);
  bool finalizeDisconnectedDevice(DeviceState &device);
  bool drainClientTransfers(uint32_t timeoutMs);
  bool releaseClientResources();
  bool uninstallHostLibrary(uint32_t timeoutMs);
  void configureCdcAcm(DeviceState &device, SerialPortState &port);
  void configureVendorSerial(DeviceState &device, SerialPortState &port);
  bool submitInputTransfer(EndpointState &endpoint);
  bool submitHIDReportDescriptorRequest(const HIDReportDescriptorState &descriptor);
  void submitPendingTransfers(usb_device_handle_t deviceHandle, uint8_t interfaceNumber);
  bool submitSetInterface(DeviceState &device, uint8_t interfaceNumber, uint8_t alternateSetting);
  bool claimNetworkInterface(DeviceState &device, const EspUsbHostNetworkInterfaceInfo &network);
  void releaseNetworkInterface(DeviceState &device);
  // Reads GET_NTB_PARAMETERS and, when the device allows it, caps the
  // device->host NTB size with SET_NTB_INPUT_SIZE. Returns the size to allocate
  // the receive buffer with and to submit the bulk-IN transfer with.
  uint16_t negotiateNetworkNtbInput(DeviceState &device, const EspUsbHostNetworkInterfaceInfo &network);
  bool startNetworkEndpoints(DeviceState &device);
  void handleNetworkInput(DeviceState &device, EndpointState &endpoint, const uint8_t *data, size_t length);
  void parseNetworkNtb(DeviceState &device, const uint8_t *data, size_t length);
  void handleNetworkNotification(DeviceState &device, const uint8_t *data, size_t length);
  void deliverNetworkFrame(DeviceState &device, const uint8_t *frame, size_t length);
  size_t buildNcmFrame(uint8_t *out, size_t outCapacity, const uint8_t *frame, size_t length, uint16_t sequence);
  bool networkSendFrameInternal(DeviceState &device, const uint8_t *frame, size_t length);
  bool networkSendLocked(DeviceState &device, const uint8_t *frame, size_t length);
  void networkDrainTx(DeviceState &device);
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  // esp_netif (lwIP) attach/detach. The netif transmit hook reuses the public
  // networkWriteFrame(); esp_netif headers are only pulled into the .cpp.
  bool networkStartNetif(DeviceState &device, const EspUsbHostNetworkConfig &config);
  void networkStopNetif(DeviceState &device);
  // Reads the CDC iMACAddress string descriptor into mac[6] (12 hex chars).
  // Returns false when the device advertises no MAC string (index 0) or on error.
  bool readNetworkMac(DeviceState &device, uint8_t mac[6]);
#endif
  void clearParsedDescriptorState(DeviceState &device);
  bool submitAudioSamplingFrequency(DeviceState &device, uint8_t endpointAddress, uint32_t sampleRate);
  // UAC2 replaces the UAC1 endpoint sampling frequency control with a 4-byte
  // SAM_FREQ_CONTROL on the Clock Source entity, addressed through the Audio
  // Control interface.
  bool submitAudioClockSampleRate(DeviceState &device, uint8_t clockSourceId, uint32_t sampleRate);
  // Pushes a rate to whichever control the stream's class revision uses.
  bool applyAudioStreamSampleRate(DeviceState &device,
                                  const EspUsbHostAudioStreamInfo &stream,
                                  uint32_t sampleRate);
  // Starts the RANGE queries for every Clock Source referenced by the device's
  // UAC2 streams. Called once the configuration descriptor has been parsed.
  void queryAudioClockSampleRates(DeviceState &device);
  // Kicks off the asynchronous SAM_FREQ_CONTROL RANGE query that fills a UAC2
  // stream's sampleRates[]. attemptIndex walks the wLength strategies described in
  // audioClockRangeTransferCallback().
  bool submitAudioClockSampleRateRange(DeviceState &device, uint8_t clockSourceId, uint8_t attemptIndex);
  static void audioClockRangeTransferCallback(usb_transfer_t *transfer);
  void applyAudioClockSampleRates(DeviceState &device,
                                  uint8_t clockSourceId,
                                  const uint32_t *rates,
                                  size_t rateCount,
                                  uint32_t currentRate);
  bool audioFeatureControl(DeviceState &device,
                           uint8_t request,
                           uint8_t unitId,
                           uint8_t controlSelector,
                           uint8_t channel,
                           uint8_t *data,
                           size_t length,
                           bool dataIn,
                           uint32_t timeoutMs);
  bool submitAudioOutputTransfer(DeviceState &device, const uint8_t *data, size_t length);
  bool submitAudioOutputRequestTransfer(DeviceState &device, usb_transfer_t *transfer);
  bool fillAudioOutputTransfer(DeviceState &device, usb_transfer_t *transfer);
  bool isManagedAudioOutputTransfer(const DeviceState &device, const usb_transfer_t *transfer) const;
  void releaseAudioOutputTransfers(DeviceState &device);
  // Explicit feedback endpoint polling. The transfer is not an EndpointState entry
  // for the same reason the audio OUT transfers are not: it is owned by the
  // playback stream and lives only while it runs.
  uint32_t audioOutputPacingRate(const DeviceState &device) const;
  bool startAudioFeedback(DeviceState &device);
  bool submitAudioFeedbackTransfer(DeviceState &device);
  void applyAudioFeedback(DeviceState &device, const usb_transfer_t *transfer);
  void releaseAudioFeedbackTransfer(DeviceState &device);
  static void audioFeedbackTransferCallback(usb_transfer_t *transfer);
  bool mscCommand(DeviceState &device,
                  const uint8_t *command,
                  uint8_t commandLength,
                  uint8_t *data,
                  size_t dataLength,
                  bool dataIn,
                  uint32_t timeoutMs);
  bool mscClearEndpointHalt(DeviceState &device, uint8_t endpointAddress, uint32_t timeoutMs);
  bool mscResetRecovery(DeviceState &device, uint32_t timeoutMs);
  void mscUnmountAddress(uint8_t address);
  void mscUnmountAll();
  bool submitVendorSerialControl(uint8_t requestType,
                                 uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 const uint8_t *data = nullptr,
                                 size_t length = 0,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool submitVendorControl(DeviceState &device,
                           uint8_t requestType,
                           uint8_t request,
                           uint16_t value,
                           uint16_t index,
                           uint8_t *data,
                           size_t length,
                           size_t *actualLength,
                           uint32_t timeoutMs);
  void attachCdcSerial(EspUsbHostCdcSerial *serial);
  void detachCdcSerial(EspUsbHostCdcSerial *serial);
  void setLastError(esp_err_t err);
  static String usbString(const usb_str_desc_t *strDesc);
  friend class EspUsbHostCdcSerial;

  template <typename Callback>
  void setHIDCallback(std::shared_ptr<Callback> &target, Callback callback);
  template <typename Callback, size_t Capacity>
  EspUsbHostListenerId addHIDListener(ListenerRegistry<Callback, Capacity> &registry, Callback callback);
  template <typename Callback, size_t Capacity>
  bool removeHIDListenerLocked(ListenerRegistry<Callback, Capacity> &registry, EspUsbHostListenerId listenerId);
  template <typename Callback, size_t Capacity>
  bool listenerIdInUseLocked(const ListenerRegistry<Callback, Capacity> &registry,
                             EspUsbHostListenerId listenerId) const;
  template <typename Callback, size_t Capacity>
  size_t snapshotHIDCallbacks(const std::shared_ptr<Callback> &single,
                              const ListenerRegistry<Callback, Capacity> &registry,
                              std::shared_ptr<Callback> &singleSnapshot,
                              std::shared_ptr<Callback> *listenerSnapshots);
  void dispatchDeviceConnected(const EspUsbHostDeviceInfo &info);
  void dispatchDeviceDisconnected(const EspUsbHostDeviceInfo &info);
  EspUsbHostListenerId allocateListenerIdLocked();
  bool listenerIdInUseLocked(EspUsbHostListenerId listenerId) const;
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  static bool enumerationFilterCallback(const usb_device_desc_t *deviceDescriptor,
                                        uint8_t *configurationValue);
  static EspUsbHost *enumerationHost_;
#endif

  EspUsbHostConfig config_;
  TaskHandle_t taskHandle_ = nullptr;
  TaskHandle_t clientTaskHandle_ = nullptr;
  volatile bool running_ = false;
  volatile bool ready_ = false;
  // Guards the check-and-claim of EndpointState::transferSubmitted. One lock for
  // every endpoint rather than one each: it is held for a compare and a store,
  // never across the driver call, so contention is not a consideration and a
  // portMUX_TYPE per endpoint would cost more than it saves.
  portMUX_TYPE endpointSubmitMux_ = portMUX_INITIALIZER_UNLOCKED;
  esp_err_t lastError_ = ESP_OK;

  usb_host_client_handle_t clientHandle_ = nullptr;
  // A device counts as a keyboard when it declared a boot keyboard interface or
  // when the report descriptor revealed a keyboard input report.
  static bool deviceHasKeyboard(const DeviceState &device);
  // Resolve where a keyboard LED Set_Report must go: boot interface with report
  // ID 0 when declared, otherwise the LED output report learned from the report
  // descriptor. False when the device has no known LED output.
  static bool keyboardLedTarget(const DeviceState &device, uint8_t &interfaceNumber, uint8_t &reportId);
  bool sendKeyboardLedReport(DeviceState &device, uint8_t leds);
  DeviceState devices_[ESP_USB_HOST_MAX_DEVICES];
  DeviceState *currentDevice_ = nullptr;
  EspUsbHostCdcSerial *cdcSerials_[ESP_USB_HOST_MAX_CDC_SERIALS] = {};
  EspUsbHostSerialConfig defaultSerialConfig_;
  uint32_t defaultAudioSampleRate_ = 48000;
  uint8_t nextHubIndex_ = 1;
  bool hubTrackingEnabled_ = true;
  uint32_t lastHostDeviceScanMs_ = 0;

  EndpointState endpoints_[16];
  uint8_t currentInterfaceNumber_ = 0;
  uint8_t currentInterfaceAlternate_ = 0;
  uint8_t currentInterfaceClass_ = 0;
  uint8_t currentInterfaceSubClass_ = 0;
  uint8_t currentInterfaceProtocol_ = 0;
  uint8_t currentAudioChannels_ = 0;
  uint8_t currentAudioBytesPerSample_ = 0;
  uint8_t currentAudioBitsPerSample_ = 0;
  uint32_t currentAudioSampleRate_ = 0;
  uint8_t currentAudioSampleRateCount_ = 0;
  uint32_t currentAudioSampleRates_[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
  uint32_t currentAudioSampleRateMin_ = 0;
  uint32_t currentAudioSampleRateMax_ = 0;
  uint32_t currentAudioSampleRateResolution_ = 0;
  // bTerminalLink of the Audio Streaming interface being parsed (UAC2 AS_GENERAL).
  uint8_t currentAudioTerminalLink_ = 0;
  // Format-level fields of the VideoStreaming Format descriptor the scan is
  // inside. Each Frame descriptor that follows copies these and adds its own
  // size and rates, which is how the two-level descriptor tree is flattened.
  EspUsbHostVideoStreamInfo currentVideoFormat_;
  bool currentVideoFormatValid_ = false;
  bool currentInterfaceClaimed_ = false;
  esp_err_t currentClaimResult_ = ESP_OK;
  // Direction of the MIDI Streaming bulk endpoint the scan just passed, so the
  // class-specific endpoint descriptor that follows can be attributed to it.
  // ESP_USB_HOST_MIDI_ENDPOINT_NONE while the scan is not directly after one.
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_NONE = 0;
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_IN = 1;
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_OUT = 2;
  uint8_t currentMidiEndpointDirection_ = ESP_USB_HOST_MIDI_ENDPOINT_NONE;
  // Index into DeviceState::serialPorts for the CDC function whose descriptors
  // are being walked, so the Union functional descriptor and the endpoints that
  // follow land on the right port. 0xff outside a serial function.
  uint8_t currentSerialPortIndex_ = 0xff;

  EspUsbHostKeyboardLayout keyboardLayout_ = ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US;

  std::shared_ptr<DeviceCallback> deviceConnectedCallback_;
  std::shared_ptr<DeviceCallback> deviceDisconnectedCallback_;
  std::shared_ptr<KeyboardCallback> keyboardCallback_;
  std::shared_ptr<KeyboardStateCallback> keyboardStateCallback_;
  std::shared_ptr<MouseCallback> mouseCallback_;
  HIDInputCallback hidInputCallback_;
  HIDReportDescriptorCallback hidReportDescriptorCallback_;
  SerialDataCallback serialDataCallback_;
  std::shared_ptr<MidiMessageCallback> midiMessageCallback_;
  VideoFrameCallback videoFrameCallback_;
  AudioDataCallback audioDataCallback_;
  AudioOutputCallback audioOutputCallback_;
  std::shared_ptr<ConsumerControlCallback> consumerControlCallback_;
  std::shared_ptr<GamepadCallback> gamepadCallback_;
  HIDVendorInputCallback hidVendorInputCallback_;
  VendorDataCallback vendorDataCallback_;
  CcidSlotChangeCallback ccidCardInsertedCallback_;
  CcidSlotChangeCallback ccidCardRemovedCallback_;
  // Guards the vendor bulk OUT slot-state scan against concurrent callers and
  // against the completion callback on the USB client task.
  portMUX_TYPE vendorOutMux_ = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE serialOutMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::shared_ptr<SystemControlCallback> systemControlCallback_;
  NetworkFrameCallback networkFrameCallback_;
  ConfigurationSelector configurationSelector_;
  ListenerRegistry<KeyboardCallback> keyboardListeners_;
  ListenerRegistry<KeyboardStateCallback> keyboardStateListeners_;
  ListenerRegistry<MouseCallback> mouseListeners_;
  ListenerRegistry<ConsumerControlCallback> consumerControlListeners_;
  ListenerRegistry<SystemControlCallback> systemControlListeners_;
  ListenerRegistry<GamepadCallback> gamepadListeners_;
  ListenerRegistry<MidiMessageCallback> midiMessageListeners_;
  ListenerRegistry<DeviceCallback, ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS> deviceConnectedListeners_;
  ListenerRegistry<DeviceCallback, ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS> deviceDisconnectedListeners_;
  SemaphoreHandle_t hidCallbackMutex_ = nullptr;
  EspUsbHostListenerId nextListenerId_ = 1;
};

class EspUsbHostMscFS : public fs::FS
{
public:
  EspUsbHostMscFS();
  ~EspUsbHostMscFS();

  bool begin(EspUsbHost &host,
             const char *basePath = "/usb",
             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
             uint8_t lun = 0,
             uint8_t maxFiles = 4,
             uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
             bool skipSyncCache = false);
  void end();
  bool mounted() const;
  const char *basePath() const;
  void setSkipSyncCache(bool skip);
  bool skipSyncCache() const;

private:
  EspUsbHost *host_ = nullptr;
  char basePath_[16] = {};
  bool skipSyncCache_ = false;
};

class EspUsbHostCdcSerial : public Stream
{
public:
  explicit EspUsbHostCdcSerial(EspUsbHost &host);
  ~EspUsbHostCdcSerial();

  bool setRxBufferSize(size_t size);
  size_t rxBufferSize() const;

  bool begin(uint32_t baud = 115200);
  void end();
  bool connected() const;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;

  bool setBaudRate(uint32_t baud);
  bool setConfig(const EspUsbHostSerialConfig &config);
  bool setDtr(bool enable);
  bool setRts(bool enable);
  void setAddress(uint8_t address);
  uint8_t address() const;
  void clearAddress();
  // Which CDC port of the selected device this Stream is bound to, numbered from
  // 0 in descriptor order. Left at ESP_USB_HOST_ANY_PORT the object follows the
  // device's first ready port, which is what a single-port device gives. Bind one
  // object per port to drive a composite device that publishes several:
  //
  //   EspUsbHostCdcSerial portA(usb), portB(usb);
  //   portA.setAddress(address); portA.setPort(0); portA.begin(115200);
  //   portB.setAddress(address); portB.setPort(1); portB.begin(115200);
  void setPort(uint8_t port);
  uint8_t port() const;
  void clearPort();

private:
  void pushData(const uint8_t *data, size_t length);
  bool accepts(uint8_t address, uint8_t port, uint8_t defaultPort) const;
  size_t nextIndex(size_t index) const;
  bool allocateRxBuffer();
  friend class EspUsbHost;

  EspUsbHost &host_;
  uint8_t address_ = ESP_USB_HOST_ANY_ADDRESS;
  uint8_t port_ = ESP_USB_HOST_ANY_PORT;
  // Allocated by begin() (or early by setRxBufferSize()) rather than embedded,
  // so the size can be chosen from the sketch without changing sizeof(*this).
  uint8_t *rxBuffer_ = nullptr;
  size_t rxBufferSize_ = ESP_USB_HOST_CDC_RX_BUFFER_SIZE;
  bool attached_ = false;
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
};

#endif
