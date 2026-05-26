#ifndef ESP_USB_HOST_H
#define ESP_USB_HOST_H

#include <Arduino.h>
#include <functional>
#include <usb/usb_host.h>
#include <class/hid/hid.h>

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

static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF = 0x01;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_STANDBY = 0x02;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST = 0x03;
static constexpr uint8_t ESP_USB_HOST_ANY_ADDRESS = 0xff;
static constexpr size_t ESP_USB_HOST_MAX_DEVICES = 8;
static constexpr size_t ESP_USB_HOST_MAX_INTERFACES = 16;
static constexpr size_t ESP_USB_HOST_MAX_ENDPOINTS = 16;
static constexpr size_t ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS = 8;
static constexpr size_t ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE = 512;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_STREAMS = 8;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES = 4;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS = 4;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS = 8;
static constexpr size_t ESP_USB_HOST_MAX_CDC_SERIALS = 4;
static constexpr size_t ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS = 4;
static constexpr uint32_t ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS = 5000;
static constexpr uint32_t ESP_USB_HOST_AUDIO_CONTROL_DEFAULT_TIMEOUT_MS = 1000;

struct EspUsbHostConfig
{
  uint32_t taskStackSize = 8192;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
  EspUsbHostPort port = ESP_USB_HOST_PORT_DEFAULT;
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
  uint8_t modifiers = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
};

struct EspUsbHostMouseEvent : EspUsbHostHIDReportData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  uint8_t buttons = 0;
  uint8_t previousButtons = 0;
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
};

struct EspUsbHostAudioFeatureUnitInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t unitId = 0;
  uint8_t sourceId = 0;
  uint8_t channelCount = 0;
  uint8_t controlSize = 0;
  uint32_t masterControls = 0;
  uint32_t channelControls[ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS] = {};
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
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostHIDReportDescriptor &descriptor, Print &out = Serial);
void espUsbHostPrintHIDReportDescriptor(const uint8_t *data, size_t length, Print &out = Serial);

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

struct EspUsbHostVendorInput : EspUsbHostHIDReportData
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
  using MouseCallback = std::function<void(const EspUsbHostMouseEvent &)>;
  using HIDInputCallback = std::function<void(const EspUsbHostHIDInput &)>;
  using HIDReportDescriptorCallback = std::function<void(const EspUsbHostHIDReportDescriptor &)>;
  using SerialDataCallback = std::function<void(const EspUsbHostSerialData &)>;
  using MidiMessageCallback = std::function<void(const EspUsbHostMidiMessage &)>;
  using AudioDataCallback = std::function<void(const EspUsbHostAudioData &)>;
  using AudioOutputCallback = std::function<void(EspUsbHostAudioOutputRequest &)>;
  using ConsumerControlCallback = std::function<void(const EspUsbHostConsumerControlEvent &)>;
  using GamepadCallback = std::function<void(const EspUsbHostGamepadEvent &)>;
  using VendorInputCallback = std::function<void(const EspUsbHostVendorInput &)>;
  using SystemControlCallback = std::function<void(const EspUsbHostSystemControlEvent &)>;

  EspUsbHost();
  ~EspUsbHost();

  bool begin();
  bool begin(const EspUsbHostConfig &config);
  void end();
  bool ready() const;

  void onDeviceConnected(DeviceCallback callback);
  void onDeviceDisconnected(DeviceCallback callback);
  void onKeyboard(KeyboardCallback callback);
  void onMouse(MouseCallback callback);
  void onHIDInput(HIDInputCallback callback);
  void onHIDReportDescriptor(HIDReportDescriptorCallback callback);
  void onSerialData(SerialDataCallback callback);
  void onMidiMessage(MidiMessageCallback callback);
  void onAudioData(AudioDataCallback callback);
  void onAudioOutputRequest(AudioOutputCallback callback);
  void onConsumerControl(ConsumerControlCallback callback);
  void onGamepad(GamepadCallback callback);
  void onVendorInput(VendorInputCallback callback);
  void onSystemControl(SystemControlCallback callback);

  void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
  bool sendSetProtocol(uint8_t interfaceNumber, uint8_t address);
  bool sendHIDReport(uint8_t interfaceNumber,
                     uint8_t reportType,
                     uint8_t reportId,
                     const uint8_t *data,
                     size_t length,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendVendorOutput(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendVendorFeature(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendSerial(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendSerial(const char *text, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool setSerialBaudRate(uint32_t baud, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool setSerialConfig(const EspUsbHostSerialConfig &config, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
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
  bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                        uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
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
  bool getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change);
  bool getKeyboardNumLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardCapsLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardScrollLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t deviceCount() const;
  size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
  bool getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
  size_t getHostDeviceAddresses(uint8_t *addresses, size_t maxAddresses) const;
  bool probeHostDevice(uint8_t address, EspUsbHostDeviceProbeInfo &probe);
  bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
  size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const;
  size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const;
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
    bool resubmitAfterLed = false;
    uint8_t lastKeyboardReport[8] = {};
    bool keyboardReportReady = false;
    uint8_t lastMouseButtons = 0;
    uint16_t lastConsumerUsage = 0;
    EspUsbHostGamepadPrevState lastGamepadState;
    EspUsbHostHIDFieldValue hidFieldValues[ESP_USB_HOST_MAX_HID_EVENT_FIELDS] = {};
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
    bool serialDtr = true;
    bool serialRts = true;
    bool hasVendorSerialInterface = false;
    bool vendorSerialSupported = false;
    uint8_t vendorSerialInterfaceNumber = 0;
    bool hasMidiInterface = false;
    uint8_t midiInterfaceNumber = 0;
    bool hasMidiOutEndpoint = false;
    uint8_t midiOutEndpointAddress = 0;
    uint16_t midiOutPacketSize = 0;
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
    uint32_t audioSampleRate = 48000;
    uint8_t audioControlInterfaceNumber = 0xff;
    EspUsbHostAudioFeatureUnitInfo audioFeatureUnits[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS] = {};
    uint8_t audioFeatureUnitCount = 0;
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
    EspUsbHostAudioStreamInfo audioStreamInfos[ESP_USB_HOST_MAX_AUDIO_STREAMS] = {};
    uint8_t audioStreamInfoCount = 0;
    EspUsbHostInterfaceInfo interfaceInfos[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceInfoCount = 0;
    EspUsbHostEndpointInfo endpointInfos[ESP_USB_HOST_MAX_ENDPOINTS] = {};
    uint8_t endpointInfoCount = 0;
    uint8_t endpointChannelCount = 0;
    HIDReportDescriptorState hidReportDescriptors[ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS] = {};
    uint8_t hidReportDescriptorCount = 0;
    HIDInputFieldState hidInputFields[ESP_USB_HOST_MAX_HID_INPUT_FIELDS] = {};
    size_t hidInputFieldCount = 0;
    uint8_t interfaces[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceCount = 0;
    bool isHub = false;
    uint8_t hubIndex = 0;
  };

  static void taskEntry(void *arg);
  static void clientTaskEntry(void *arg);
  static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  static void transferCallback(usb_transfer_t *transfer);
  static void controlTransferCallback(usb_transfer_t *transfer);
  static void hidReportDescriptorTransferCallback(usb_transfer_t *transfer);
  static void outputTransferCallback(usb_transfer_t *transfer);
  static void serialOutTransferCallback(usb_transfer_t *transfer);

  void taskLoop();
  void clientTaskLoop();
  void handleClientEvent(const usb_host_client_event_msg_t *eventMsg);
  void handleNewDevice(uint8_t address);
  void handleDeviceGone(usb_device_handle_t goneHandle);
  void scanHostDevices();
  void refreshDeviceTopology(DeviceState &device);
  void parseConfigDescriptor(DeviceState &device, const usb_config_desc_t *configDesc);
  void handleDescriptor(uint8_t descriptorType, const uint8_t *data);
  void parseAudioControlDescriptor(DeviceState &device, const uint8_t *data);
  void recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input);
  void handleTransfer(usb_transfer_t *transfer);
  void handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMidi(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleAudio(EndpointState &endpoint, usb_transfer_t *transfer);
  void handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength);
  void parseHIDReportDescriptor(DeviceState &device, const EspUsbHostHIDReportDescriptor &descriptor);
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
  DeviceState *findVendorDevice(uint8_t address);
  void releaseEndpoints(DeviceState &device, bool clearEndpoints);
  void releaseAllEndpoints(bool clearEndpoints);
  void releaseInterfaces(DeviceState &device);
  void configureCdcAcm(DeviceState &device);
  void configureVendorSerial(DeviceState &device);
  bool submitInputTransfer(EndpointState &endpoint);
  bool submitHIDReportDescriptorRequest(const HIDReportDescriptorState &descriptor);
  void submitPendingTransfers(usb_device_handle_t deviceHandle, uint8_t interfaceNumber);
  bool submitSetInterface(DeviceState &device, uint8_t interfaceNumber, uint8_t alternateSetting);
  bool submitAudioSamplingFrequency(DeviceState &device, uint8_t endpointAddress, uint32_t sampleRate);
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
  bool mscCommand(DeviceState &device,
                  const uint8_t *command,
                  uint8_t commandLength,
                  uint8_t *data,
                  size_t dataLength,
                  bool dataIn,
                  uint32_t timeoutMs);
  bool mscResetRecovery(DeviceState &device, uint32_t timeoutMs);
  bool submitVendorSerialControl(uint8_t requestType,
                                 uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 const uint8_t *data = nullptr,
                                 size_t length = 0,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void attachCdcSerial(EspUsbHostCdcSerial *serial);
  void detachCdcSerial(EspUsbHostCdcSerial *serial);
  void setLastError(esp_err_t err);
  static String usbString(const usb_str_desc_t *strDesc);
  friend class EspUsbHostCdcSerial;

  EspUsbHostConfig config_;
  TaskHandle_t taskHandle_ = nullptr;
  TaskHandle_t clientTaskHandle_ = nullptr;
  volatile bool running_ = false;
  volatile bool ready_ = false;
  esp_err_t lastError_ = ESP_OK;

  usb_host_client_handle_t clientHandle_ = nullptr;
  bool sendKeyboardLedReport(DeviceState &device, uint8_t leds);
  DeviceState devices_[ESP_USB_HOST_MAX_DEVICES];
  DeviceState *currentDevice_ = nullptr;
  EspUsbHostCdcSerial *cdcSerials_[ESP_USB_HOST_MAX_CDC_SERIALS] = {};
  EspUsbHostSerialConfig defaultSerialConfig_;
  uint32_t defaultAudioSampleRate_ = 48000;
  uint8_t nextHubIndex_ = 1;
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
  esp_err_t currentClaimResult_ = ESP_OK;

  EspUsbHostKeyboardLayout keyboardLayout_ = ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US;

  DeviceCallback deviceConnectedCallback_;
  DeviceCallback deviceDisconnectedCallback_;
  KeyboardCallback keyboardCallback_;
  MouseCallback mouseCallback_;
  HIDInputCallback hidInputCallback_;
  HIDReportDescriptorCallback hidReportDescriptorCallback_;
  SerialDataCallback serialDataCallback_;
  MidiMessageCallback midiMessageCallback_;
  AudioDataCallback audioDataCallback_;
  AudioOutputCallback audioOutputCallback_;
  ConsumerControlCallback consumerControlCallback_;
  GamepadCallback gamepadCallback_;
  VendorInputCallback vendorInputCallback_;
  SystemControlCallback systemControlCallback_;
};

class EspUsbHostCdcSerial : public Stream
{
public:
  explicit EspUsbHostCdcSerial(EspUsbHost &host);

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
  static constexpr size_t RX_BUFFER_SIZE = 512;

  void pushData(const uint8_t *data, size_t length);
  bool accepts(uint8_t address) const;
  size_t nextIndex(size_t index) const;
  friend class EspUsbHost;

  EspUsbHost &host_;
  uint8_t address_ = ESP_USB_HOST_ANY_ADDRESS;
  uint8_t rxBuffer_[RX_BUFFER_SIZE] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
};

#endif
