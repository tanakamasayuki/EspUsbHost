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
static constexpr size_t ESP_USB_HOST_MAX_CDC_SERIALS = 4;
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
static constexpr uint32_t ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS = 1000;
// Upper bound for vendorWriteQueueBegin(depth, ...). Each slot holds one
// preallocated transfer, so the practical depth is limited by DMA memory rather
// than by this constant.
static constexpr size_t ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH = 8;
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
  const uint8_t *data = nullptr;
  size_t length = 0;
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

void espUsbHostPrintHex(const uint8_t *data, size_t length, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostInterfaceInfo &intf, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostNetworkInterfaceInfo &network, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream, Print &out = Serial);
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
  bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t interfaceNumber = 0xff,
                  EspUsbHostVendorReadMode readMode = ESP_USB_HOST_VENDOR_READ_CONTINUOUS);
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
  bool sendSerial(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendSerial(const char *text, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool setSerialBaudRate(uint32_t baud, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool setSerialConfig(const EspUsbHostSerialConfig &config, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Max packet size of the CDC data OUT endpoint, or 0 when no serial device is
  // ready. Needed by callers that must terminate a transfer on a packet boundary.
  uint16_t serialOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

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
  bool serialWriteQueueBegin(size_t depth,
                             size_t bufferBytes,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void serialWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool serialWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  uint8_t *serialWriteAcquire(size_t *capacity,
                              uint32_t timeoutMs = 0,
                              uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool serialWriteSubmit(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void serialWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  // Copies into a pooled buffer and submits it. Fails when length exceeds the
  // per-slot buffer size; the caller decides how to split.
  bool serialWriteAsync(const uint8_t *data,
                        size_t length,
                        uint32_t timeoutMs = 0,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  size_t serialWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t serialWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool serialWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  EspUsbHostSerialWriteStats serialWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  void serialWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

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
  size_t maxEndpointChannelCount() const;
  size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams, size_t maxStreams) const;

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
    bool hasCdcControlInterface = false;
    bool hasCdcDataInterface = false;
    bool cdcConfigured = false;
    uint8_t cdcControlInterfaceNumber = 0;
    uint8_t cdcDataInterfaceNumber = 0;
    bool hasSerialOutEndpoint = false;
    uint8_t serialOutEndpointAddress = 0;
    uint16_t serialOutPacketSize = 0;
    EspUsbHostSerialConfig serialConfig;
    // Asynchronous CDC OUT queue, same shape as the vendor bulk OUT queue below.
    bool serialOutQueueActive = false;
    uint8_t serialOutQueueDepth = 0;
    size_t serialOutBufferBytes = 0;
    usb_transfer_t *serialOutTransfers[ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH] = {};
    uint8_t serialOutSlotState[ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH] = {};
    SemaphoreHandle_t serialOutFreeSlots = nullptr;
    bool serialOutHalted = false;
    EspUsbHostSerialWriteStats serialWriteStats;
    bool serialDtr = true;
    bool serialRts = true;
    bool hasVendorSerialInterface = false;
    bool vendorSerialSupported = false;
    uint8_t vendorSerialInterfaceNumber = 0;
    bool hasUsbVendorInterface = false;
    uint8_t usbVendorInterfaceNumber = 0xff;
    bool usbVendorReadOnDemand = false;
    bool hasUsbVendorInEndpoint = false;
    uint8_t usbVendorInEndpointAddress = 0;
    uint16_t usbVendorInPacketSize = 0;
    bool hasUsbVendorOutEndpoint = false;
    uint8_t usbVendorOutEndpointAddress = 0;
    uint16_t usbVendorOutPacketSize = 0;
    uint8_t usbVendorRxBuffer[ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE] = {};
    size_t usbVendorRxHead = 0;
    size_t usbVendorRxTail = 0;
    size_t usbVendorRxCount = 0;
    // Asynchronous bulk OUT queue. Slots are preallocated by
    // vendorWriteQueueBegin() and reused; usbVendorOutFreeSlots counts the slots
    // that are neither acquired nor in flight.
    bool usbVendorOutQueueActive = false;
    uint8_t usbVendorOutQueueDepth = 0;
    size_t usbVendorOutBufferBytes = 0;
    usb_transfer_t *usbVendorOutTransfers[ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH] = {};
    uint8_t usbVendorOutSlotState[ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH] = {};
    SemaphoreHandle_t usbVendorOutFreeSlots = nullptr;
    bool usbVendorOutHalted = false;
    bool usbVendorAutoZlp = false;
    EspUsbHostVendorWriteStats usbVendorWriteStats;
    bool hasMidiInterface = false;
    uint8_t midiInterfaceNumber = 0;
    bool hasMidiOutEndpoint = false;
    uint8_t midiOutEndpointAddress = 0;
    uint16_t midiOutPacketSize = 0;
    uint8_t midiInCableCount = 0;
    uint8_t midiOutCableCount = 0;
    bool hasAudioInterface = false;
    uint8_t audioInterfaceNumber = 0;
    bool hasAudioInEndpoint = false;
    uint8_t audioInInterfaceNumber = 0;
    uint8_t audioInAlternate = 0;
    uint8_t audioInEndpointAddress = 0;
    uint8_t audioInChannels = 0;
    uint8_t audioInBytesPerSample = 0;
    uint8_t audioInBitsPerSample = 0;
    bool hasAudioOutEndpoint = false;
    uint8_t audioOutInterfaceNumber = 0;
    uint8_t audioOutEndpointAddress = 0;
    uint16_t audioOutPacketSize = 0;
    uint8_t audioOutChannels = 0;
    uint8_t audioOutBytesPerSample = 0;
    uint8_t audioOutBitsPerSample = 0;
    uint8_t audioOutInterval = 0;
    bool audioOutRunning = false;
    uint32_t audioOutFrameAccumulator = 0;
    uint32_t audioOutUnderruns = 0;
    usb_transfer_t *audioOutTransfers[ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS] = {};
    // Explicit feedback endpoint of an asynchronous playback interface, when the
    // claimed alternate declares one. audioOutFeedbackRate is the last plausible
    // rate the device asked for; audioOutRate() falls back to the negotiated rate
    // while it is 0, so a synchronous device behaves exactly as before.
    uint8_t audioOutFeedbackInterfaceNumber = 0xff;
    uint8_t audioOutFeedbackEndpointAddress = 0;
    uint16_t audioOutFeedbackPacketSize = 0;
    uint8_t audioOutFeedbackInterval = 0;
    usb_transfer_t *audioOutFeedbackTransfer = nullptr;
    uint32_t audioOutFeedbackRate = 0;
    uint32_t audioOutFeedbackUpdates = 0;
    uint32_t audioOutFeedbackRejects = 0;
    uint32_t audioSampleRate = 48000;
    uint8_t audioControlInterfaceNumber = 0xff;
    // bInterfaceProtocol of the device's Audio interfaces (0x20 for UAC2), taken
    // from the Audio Control interface and reused for its streaming interfaces.
    uint8_t audioProtocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
    EspUsbHostAudioFeatureUnitInfo audioFeatureUnits[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS] = {};
    uint8_t audioFeatureUnitCount = 0;
    // UAC2 clock topology. Clock Source entities carry the sample frequency
    // control, and the Input/Output Terminal a streaming interface links to names
    // the clock that drives it.
    AudioClockSourceState audioClockSources[ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES] = {};
    uint8_t audioClockSourceCount = 0;
    AudioTerminalClockLink audioTerminalClocks[ESP_USB_HOST_MAX_AUDIO_TERMINALS] = {};
    uint8_t audioTerminalClockCount = 0;
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
    EspUsbHostAudioStreamInfo audioStreamInfos[ESP_USB_HOST_MAX_AUDIO_STREAMS] = {};
    uint8_t audioStreamInfoCount = 0;
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
  DeviceState *findSerialDevice(uint8_t address);
  const DeviceState *findSerialDevice(uint8_t address) const;
  DeviceState *findMidiDevice(uint8_t address);
  const DeviceState *findMidiDevice(uint8_t address) const;
  DeviceState *findAudioOutputDevice(uint8_t address);
  const DeviceState *findAudioOutputDevice(uint8_t address) const;
  DeviceState *findAudioInputDevice(uint8_t address);
  const DeviceState *findAudioInputDevice(uint8_t address) const;
  const DeviceState *findAudioDevice(uint8_t address) const;
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
  int serialOutSlotOf(const DeviceState &device, const uint8_t *buffer) const;
  int serialOutSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const;
  bool submitSerialOutSlot(DeviceState &device, int slot, size_t length);
  void releaseSerialOutQueue(DeviceState &device);
  void serialDrainOut(DeviceState &device);
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
  void configureCdcAcm(DeviceState &device);
  void configureVendorSerial(DeviceState &device);
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
  bool currentInterfaceClaimed_ = false;
  esp_err_t currentClaimResult_ = ESP_OK;
  // Direction of the MIDI Streaming bulk endpoint the scan just passed, so the
  // class-specific endpoint descriptor that follows can be attributed to it.
  // ESP_USB_HOST_MIDI_ENDPOINT_NONE while the scan is not directly after one.
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_NONE = 0;
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_IN = 1;
  static constexpr uint8_t ESP_USB_HOST_MIDI_ENDPOINT_OUT = 2;
  uint8_t currentMidiEndpointDirection_ = ESP_USB_HOST_MIDI_ENDPOINT_NONE;

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

private:
  void pushData(const uint8_t *data, size_t length);
  bool accepts(uint8_t address) const;
  size_t nextIndex(size_t index) const;
  bool allocateRxBuffer();
  friend class EspUsbHost;

  EspUsbHost &host_;
  uint8_t address_ = ESP_USB_HOST_ANY_ADDRESS;
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
