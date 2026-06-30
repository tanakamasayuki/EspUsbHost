#include "EspUsbHost.h"
#include "EspUsbHostHid.h"

#include "diskio_impl.h"
#include "esp_vfs_fat.h"
#include "vfs_api.h"

#include <string.h>
#include <math.h>

#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
static const char *TAG = "EspUsbHost";
#endif

static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t USB_CLASS_AUDIO_VALUE = 0x01;
static constexpr uint8_t USB_CLASS_CDC_CONTROL_VALUE = 0x02;
static constexpr uint8_t USB_CLASS_CDC_DATA_VALUE = 0x0a;
static constexpr uint8_t USB_CLASS_MASS_STORAGE_VALUE = 0x08;
static constexpr uint8_t USB_CLASS_VENDOR_VALUE = 0xff;
static constexpr uint8_t USB_CS_INTERFACE_DESC = 0x24;
static constexpr uint8_t USB_AUDIO_SUBCLASS_AUDIO_CONTROL = 0x01;
static constexpr uint8_t USB_AUDIO_SUBCLASS_AUDIO_STREAMING = 0x02;
static constexpr uint8_t USB_AUDIO_SUBCLASS_MIDI_STREAMING = 0x03;
static constexpr uint8_t USB_AUDIO_AC_FEATURE_UNIT = 0x06;
static constexpr uint8_t USB_AUDIO_FEATURE_MUTE_CONTROL = 0x01;
static constexpr uint8_t USB_AUDIO_FEATURE_VOLUME_CONTROL = 0x02;
static constexpr uint8_t USB_MSC_SUBCLASS_SCSI = 0x06;
static constexpr uint8_t USB_MSC_PROTOCOL_BULK_ONLY = 0x50;
static constexpr uint32_t USB_MSC_CBW_SIGNATURE = 0x43425355;
static constexpr uint32_t USB_MSC_CSW_SIGNATURE = 0x53425355;
static constexpr uint8_t USB_MSC_CSW_STATUS_PASSED = 0x00;
static constexpr uint8_t USB_MSC_CSW_STATUS_PHASE_ERROR = 0x02;
static constexpr uint8_t USB_MSC_RESET_REQUEST = 0xff;
static constexpr uint8_t USB_MSC_RESET_REQUEST_TYPE = 0x21;
static constexpr uint8_t USB_MSC_GET_MAX_LUN_REQUEST = 0xfe;
static constexpr uint8_t USB_MSC_GET_MAX_LUN_REQUEST_TYPE = 0xa1;
static constexpr uint8_t SCSI_CMD_TEST_UNIT_READY = 0x00;
static constexpr uint8_t SCSI_CMD_REQUEST_SENSE = 0x03;
static constexpr uint8_t SCSI_CMD_INQUIRY = 0x12;
static constexpr uint8_t SCSI_CMD_READ_CAPACITY_10 = 0x25;
static constexpr uint8_t SCSI_CMD_READ_10 = 0x28;
static constexpr uint8_t SCSI_CMD_WRITE_10 = 0x2a;
static constexpr uint8_t SCSI_CMD_SYNCHRONIZE_CACHE_10 = 0x35;
static constexpr uint8_t SCSI_CMD_READ_16 = 0x88;
static constexpr uint8_t SCSI_CMD_WRITE_16 = 0x8a;
static constexpr uint8_t SCSI_CMD_SERVICE_ACTION_IN_16 = 0x9e;
static constexpr uint8_t SCSI_SERVICE_ACTION_READ_CAPACITY_16 = 0x10;
static constexpr size_t USB_MSC_MAX_TRANSFER_BYTES = 4096;
static constexpr uint8_t USB_AUDIO_CS_AS_FORMAT_TYPE = 0x02;
static constexpr uint8_t USB_AUDIO_FORMAT_TYPE_I = 0x01;
static constexpr uint8_t HID_SUBCLASS_BOOT_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_KEYBOARD_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_MOUSE_VALUE = 0x02;
static constexpr uint8_t HID_CLASS_REQUEST_SET_REPORT = 0x09;
static constexpr uint8_t HID_CLASS_REQUEST_SET_PROTOCOL = 0x0B;
static constexpr uint8_t HID_PROTOCOL_REPORT_MODE = 0x01;
static constexpr uint8_t HID_SET_REPORT_REQUEST_TYPE = 0x21;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_LINE_CODING = 0x20;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE = 0x22;
static constexpr uint8_t CDC_SET_REQUEST_TYPE = 0x21;
static constexpr uint8_t USB_REQUEST_CLEAR_FEATURE = 0x01;
static constexpr uint8_t USB_REQUEST_GET_STATUS = 0x00;
static constexpr uint8_t USB_REQUEST_SET_FEATURE = 0x03;
static constexpr uint8_t USB_REQUEST_GET_DESCRIPTOR = 0x06;
static constexpr uint8_t USB_DESCRIPTOR_TYPE_HUB = 0x29;
static constexpr uint8_t USB_HUB_DESCRIPTOR_REQUEST_TYPE = 0xa0;
static constexpr uint8_t USB_HUB_PORT_REQUEST_TYPE = 0x23;
static constexpr uint8_t USB_HUB_PORT_IN_REQUEST_TYPE = 0xa3;
static constexpr uint16_t USB_HUB_FEATURE_PORT_POWER = 0x0008;
static constexpr uint8_t VENDOR_OUT_REQUEST_TYPE = 0x40;
static constexpr uint8_t VENDOR_INTERFACE_OUT_REQUEST_TYPE = 0x41;
static constexpr uint8_t VENDOR_IN_REQUEST_TYPE = 0xc0;
static constexpr uint8_t VENDOR_READ_REQUEST = 0x01;
static constexpr uint8_t VENDOR_WRITE_REQUEST = 0x01;
static constexpr uint8_t MIDI_CIN_SYSEX_START = 0x04;
static constexpr uint8_t MIDI_CIN_SYSEX_END_1BYTE = 0x05;
static constexpr uint8_t MIDI_CIN_SYSEX_END_2BYTE = 0x06;
static constexpr uint8_t MIDI_CIN_SYSEX_END_3BYTE = 0x07;
static constexpr uint8_t MIDI_CIN_NOTE_OFF = 0x08;
static constexpr uint8_t MIDI_CIN_NOTE_ON = 0x09;
static constexpr uint8_t MIDI_CIN_POLY_KEYPRESS = 0x0a;
static constexpr uint8_t MIDI_CIN_CONTROL_CHANGE = 0x0b;
static constexpr uint8_t MIDI_CIN_PROGRAM_CHANGE = 0x0c;
static constexpr uint8_t MIDI_CIN_CHANNEL_PRESSURE = 0x0d;
static constexpr uint8_t MIDI_CIN_PITCH_BEND_CHANGE = 0x0e;

struct EspUsbHostMscCbw
{
  uint32_t signature;
  uint32_t tag;
  uint32_t dataTransferLength;
  uint8_t flags;
  uint8_t lun;
  uint8_t commandBlockLength;
  uint8_t commandBlock[16];
} __attribute__((packed));

struct EspUsbHostMscCsw
{
  uint32_t signature;
  uint32_t tag;
  uint32_t dataResidue;
  uint8_t status;
} __attribute__((packed));

struct EspUsbHostSyncTransferContext
{
  SemaphoreHandle_t done = nullptr;
  usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
  size_t actualLength = 0;
};

struct EspUsbHostMscFatMount
{
  bool inUse = false;
  EspUsbHost *host = nullptr;
  uint8_t address = 0;
  uint8_t lun = 0;
  uint8_t pdrv = FF_DRV_NOT_USED;
  char basePath[16] = {};
  char fatDrive[8] = {};
  FATFS *fs = nullptr;
  uint64_t blockCount = 0;
  uint32_t blockSize = 0;
  bool skipSyncCache = false;
};

static EspUsbHostMscFatMount mscFatMounts[FF_VOLUMES] = {};

struct HIDReportDescriptorTransferContext
{
  EspUsbHost *host = nullptr;
  EspUsbHostHIDReportDescriptor descriptor;
};

static void syncTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHostSyncTransferContext *context = static_cast<EspUsbHostSyncTransferContext *>(transfer->context);
  if (!context)
  {
    return;
  }
  context->status = transfer->status;
  context->actualLength = transfer->actual_num_bytes;
  xSemaphoreGive(context->done);
}

static uint32_t readBe32(const uint8_t *data)
{
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

static uint64_t readBe64(const uint8_t *data)
{
  return (static_cast<uint64_t>(data[0]) << 56) |
         (static_cast<uint64_t>(data[1]) << 48) |
         (static_cast<uint64_t>(data[2]) << 40) |
         (static_cast<uint64_t>(data[3]) << 32) |
         (static_cast<uint64_t>(data[4]) << 24) |
         (static_cast<uint64_t>(data[5]) << 16) |
         (static_cast<uint64_t>(data[6]) << 8) |
         static_cast<uint64_t>(data[7]);
}

static int16_t readLe16s(const uint8_t *data)
{
  return static_cast<int16_t>(static_cast<uint16_t>(data[0]) |
                              (static_cast<uint16_t>(data[1]) << 8));
}

static void writeLe16(uint8_t *data, int16_t value)
{
  const uint16_t raw = static_cast<uint16_t>(value);
  data[0] = raw & 0xff;
  data[1] = (raw >> 8) & 0xff;
}

static int16_t audioDbToRaw(float db)
{
  const float scaled = db * 256.0f;
  return static_cast<int16_t>(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static int16_t audioClampVolumeRaw(int16_t volume, const EspUsbHostAudioVolumeRange &range)
{
  int32_t raw = volume;
  if (raw < range.min)
  {
    raw = range.min;
  }
  if (raw > range.max)
  {
    raw = range.max;
  }
  if (range.resolution > 0)
  {
    const int32_t resolution = range.resolution;
    raw = range.min + ((raw - range.min + resolution / 2) / resolution) * resolution;
    if (raw > range.max)
    {
      raw = range.max;
    }
  }
  return static_cast<int16_t>(raw);
}

static void writeBe32(uint8_t *data, uint32_t value)
{
  data[0] = (value >> 24) & 0xff;
  data[1] = (value >> 16) & 0xff;
  data[2] = (value >> 8) & 0xff;
  data[3] = value & 0xff;
}

static void writeBe64(uint8_t *data, uint64_t value)
{
  data[0] = (value >> 56) & 0xff;
  data[1] = (value >> 48) & 0xff;
  data[2] = (value >> 40) & 0xff;
  data[3] = (value >> 32) & 0xff;
  data[4] = (value >> 24) & 0xff;
  data[5] = (value >> 16) & 0xff;
  data[6] = (value >> 8) & 0xff;
  data[7] = value & 0xff;
}

static EspUsbHostMscFatMount *findMscFatMountByDrive(uint8_t pdrv)
{
  for (EspUsbHostMscFatMount &mount : mscFatMounts)
  {
    if (mount.inUse && mount.pdrv == pdrv)
    {
      return &mount;
    }
  }
  return nullptr;
}

static EspUsbHostMscFatMount *findMscFatMountByPath(const char *basePath)
{
  if (!basePath)
  {
    return nullptr;
  }
  for (EspUsbHostMscFatMount &mount : mscFatMounts)
  {
    if (mount.inUse && strcmp(mount.basePath, basePath) == 0)
    {
      return &mount;
    }
  }
  return nullptr;
}

static DSTATUS mscFatDiskInitialize(BYTE pdrv)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByDrive(pdrv);
  return mount ? 0 : STA_NOINIT;
}

static DSTATUS mscFatDiskStatus(BYTE pdrv)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByDrive(pdrv);
  return mount && mount->host && mount->host->mscReady(mount->address) ? 0 : STA_NOINIT;
}

static DRESULT mscFatDiskRead(BYTE pdrv, BYTE *buff, uint32_t sector, unsigned count)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByDrive(pdrv);
  if (!mount || !mount->host || !buff || count == 0)
  {
    return RES_PARERR;
  }
  mount->host->mscSelectLun(mount->lun, mount->address);
  return mount->host->mscReadBlocks64(sector, buff, count, mount->address) ? RES_OK : RES_ERROR;
}

static DRESULT mscFatDiskWrite(BYTE pdrv, const BYTE *buff, uint32_t sector, unsigned count)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByDrive(pdrv);
  if (!mount || !mount->host || !buff || count == 0)
  {
    return RES_PARERR;
  }
  mount->host->mscSelectLun(mount->lun, mount->address);
  return mount->host->mscWriteBlocks64(sector, buff, count, mount->address) ? RES_OK : RES_ERROR;
}

static DRESULT mscFatDiskIoctl(BYTE pdrv, BYTE cmd, void *buff)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByDrive(pdrv);
  if (!mount || !mount->host)
  {
    return RES_NOTRDY;
  }

  switch (cmd)
  {
  case CTRL_SYNC:
    if (!mount->host->mscReady(mount->address))
    {
      return RES_NOTRDY;
    }
    if (mount->skipSyncCache)
    {
      return RES_OK;
    }
    if (mount->host->mscSynchronizeCache(mount->address))
    {
      return RES_OK;
    }
    mount->skipSyncCache = true;
    ESP_LOGW(TAG, "MSC SYNCHRONIZE CACHE failed on %s, skipping it for this mount", mount->basePath);
    return RES_OK;
  case GET_SECTOR_COUNT:
    if (!buff)
    {
      return RES_PARERR;
    }
    *static_cast<LBA_t *>(buff) = static_cast<LBA_t>(mount->blockCount);
    return RES_OK;
  case GET_SECTOR_SIZE:
    if (!buff)
    {
      return RES_PARERR;
    }
    *static_cast<WORD *>(buff) = static_cast<WORD>(mount->blockSize);
    return RES_OK;
  case GET_BLOCK_SIZE:
    if (!buff)
    {
      return RES_PARERR;
    }
    *static_cast<DWORD *>(buff) = 1;
    return RES_OK;
  default:
    return RES_PARERR;
  }
}

static const ff_diskio_impl_t MSC_FAT_DISKIO = {
    .init = &mscFatDiskInitialize,
    .status = &mscFatDiskStatus,
    .read = &mscFatDiskRead,
    .write = &mscFatDiskWrite,
    .ioctl = &mscFatDiskIoctl,
};

static unsigned hostPeripheralMap(EspUsbHostPort port)
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  switch (port)
  {
  case ESP_USB_HOST_PORT_HIGH_SPEED:
    return 1U << 0;
  case ESP_USB_HOST_PORT_FULL_SPEED:
    return 1U << 1;
  case ESP_USB_HOST_PORT_DEFAULT:
  default:
    return 0;
  }
#else
  (void)port;
  return 0;
#endif
}

static bool isKnownVendorSerial(uint16_t vid, uint16_t pid)
{
  switch (vid)
  {
  case 0x0403:
    return pid == 0x6001 || pid == 0x6010 || pid == 0x6011 || pid == 0x6014 || pid == 0x6015;
  case 0x10c4:
    return pid == 0xea60 || pid == 0xea70 || pid == 0xea71;
  case 0x1a86:
    return pid == 0x5523 || pid == 0x55d3 || pid == 0x7522 || pid == 0x7523;
  case 0x067b:
    return pid == 0x2303 || pid == 0x23a3;
  default:
    return false;
  }
}

#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static const char *vendorSerialName(uint16_t vid)
{
  switch (vid)
  {
  case 0x0403:
    return "FTDI";
  case 0x10c4:
    return "CP210x";
  case 0x1a86:
    return "CH34x";
  case 0x067b:
    return "PL2303";
  default:
    return "vendor";
  }
}
#endif

static bool configHasInterfaceClass(const usb_config_desc_t *configDesc, uint8_t interfaceClass)
{
  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;)
  {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength)
    {
      return false;
    }
    if (p[i + 1] == USB_INTERFACE_DESC)
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[i]);
      if (intf->bInterfaceClass == interfaceClass)
      {
        return true;
      }
    }
    i += length;
  }
  return false;
}

static uint32_t readAudioSampleRate24(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16);
}

static const char *yesNo(bool value)
{
  return value ? "yes" : "no";
}

static const char *claimResultName(const EspUsbHostInterfaceInfo &intf)
{
  return intf.claimAttempted ? esp_err_to_name(intf.claimResult) : "not_attempted";
}

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "low-speed";
  case USB_SPEED_FULL:
    return "full-speed";
  case USB_SPEED_HIGH:
    return "high-speed";
  default:
    return "unknown";
  }
}

static const char *className(uint8_t cls)
{
  switch (cls)
  {
  case 0x00:
    return "per-interface";
  case 0x01:
    return "Audio";
  case 0x02:
    return "CDC Control";
  case 0x03:
    return "HID";
  case 0x08:
    return "Mass Storage";
  case 0x09:
    return "Hub";
  case 0x0a:
    return "CDC Data";
  case 0x0e:
    return "Video";
  case 0xff:
    return "Vendor";
  default:
    return "Unknown";
  }
}

static const char *configAttributeName(uint8_t attributes)
{
  return (attributes & 0x40) ? "self-powered" : "bus-powered";
}

static const char *transferTypeName(uint8_t attributes)
{
  switch (attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK)
  {
  case USB_BM_ATTRIBUTES_XFER_CONTROL:
    return "control";
  case USB_BM_ATTRIBUTES_XFER_ISOC:
    return "isochronous";
  case USB_BM_ATTRIBUTES_XFER_BULK:
    return "bulk";
  case USB_BM_ATTRIBUTES_XFER_INT:
    return "interrupt";
  default:
    return "unknown";
  }
}

void espUsbHostPrintHex(const uint8_t *data, size_t length, Print &out)
{
  for (size_t i = 0; i < length; i++)
  {
    if (data[i] < 0x10)
    {
      out.print('0');
    }
    out.print(data[i], HEX);
    if (i + 1 < length)
    {
      out.print(' ');
    }
  }
}

void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out)
{
  out.printf("device: address=%u portId=0x%02x vid=%04x pid=%04x class=0x%02x(%s) speed=%s product=\"%s\"\n",
             device.address,
             device.portId,
             device.vid,
             device.pid,
             device.deviceClass,
             className(device.deviceClass),
             speedName(device.speed),
             device.product);
}

void espUsbHostPrint(const EspUsbHostInterfaceInfo &intf, Print &out)
{
  out.printf("interface: number=%u alt=%u class=0x%02x(%s) subclass=0x%02x protocol=0x%02x endpoints=%u claimed=%s claim=%s\n",
             intf.number,
             intf.alternate,
             intf.interfaceClass,
             className(intf.interfaceClass),
             intf.interfaceSubClass,
             intf.interfaceProtocol,
             intf.endpointCount,
             yesNo(intf.claimed),
             claimResultName(intf));
}

void espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint, Print &out)
{
  out.printf("endpoint: iface=%u ep=0x%02x dir=%s type=%s max_packet=%u interval=%u attrs=0x%02x\n",
             endpoint.interfaceNumber,
             endpoint.address,
             (endpoint.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT",
             transferTypeName(endpoint.attributes),
             endpoint.maxPacketSize,
             endpoint.interval,
             endpoint.attributes);
}

const char *espUsbHostConsumerControlUsageName(uint16_t usage)
{
  switch (usage)
  {
  case ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK:
    return "Next";
  case ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK:
    return "Previous";
  case ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE:
    return "Play/Pause";
  case ESP_USB_HOST_CONSUMER_CONTROL_MUTE:
    return "Mute";
  case ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP:
    return "Volume Up";
  case ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN:
    return "Volume Down";
  default:
    return "";
  }
}

const char *espUsbHostSystemControlUsageName(uint8_t usage)
{
  switch (usage)
  {
  case ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF:
    return "Power Off";
  case ESP_USB_HOST_SYSTEM_CONTROL_STANDBY:
    return "Standby";
  case ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST:
    return "Wake Host";
  default:
    return "";
  }
}

void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream, Print &out)
{
  out.printf("audio stream: addr=%u iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u max_packet=%u interval=%u\n",
             stream.address,
             stream.interfaceNumber,
             stream.alternate,
             stream.endpointAddress,
             stream.input ? "IN" : stream.output ? "OUT"
                                                 : "unknown",
             stream.channels,
             stream.bytesPerSample,
             stream.bitsPerSample,
             static_cast<unsigned long>(stream.sampleRate),
             stream.sampleRateCount,
             stream.maxPacketSize,
             stream.interval);
}

void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out)
{
  static const char *modifierNames[] = {
      "LCTRL", "LSHIFT", "LALT", "LGUI",
      "RCTRL", "RSHIFT", "RALT", "RGUI"};
  const char displayChar = (event.ascii >= 0x20 && event.ascii != 0x7F) ? static_cast<char>(event.ascii) : '.';

  out.printf("keyboard: [%s] address=%u iface=%u keycode=0x%02x ascii=0x%02x(%c) modifiers=",
             event.pressed ? "press  " : "release",
             event.address,
             event.interfaceNumber,
             event.keycode,
             event.ascii,
             displayChar);
  if (event.modifiers == 0)
  {
    out.print("none");
  }
  else
  {
    bool first = true;
    for (int i = 0; i < 8; i++)
    {
      if (event.modifiers & (1 << i))
      {
        if (!first)
        {
          out.print('+');
        }
        out.print(modifierNames[i]);
        first = false;
      }
    }
  }
  out.println();
}

void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out)
{
  out.printf("hid: address=%u vid=%04x pid=%04x iface=%u subclass=0x%02x protocol=0x%02x len=%u data=",
             input.address,
             input.vid,
             input.pid,
             input.interfaceNumber,
             input.subclass,
             input.protocol,
             static_cast<unsigned>(input.length));
  espUsbHostPrintHex(input.data, input.length, out);
  out.println();
}

static int32_t hidItemValue(const uint8_t *data, size_t size)
{
  uint32_t value = 0;
  for (size_t i = 0; i < size; i++)
  {
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  }
  if (size == 1 && (value & 0x80))
  {
    return static_cast<int32_t>(value | 0xffffff00);
  }
  if (size == 2 && (value & 0x8000))
  {
    return static_cast<int32_t>(value | 0xffff0000);
  }
  return static_cast<int32_t>(value);
}

static uint32_t hidItemUnsignedValue(const uint8_t *data, size_t size)
{
  uint32_t value = 0;
  for (size_t i = 0; i < size; i++)
  {
    value |= static_cast<uint32_t>(data[i]) << (8 * i);
  }
  return value;
}

static const char *hidMainItemName(uint8_t tag)
{
  switch (tag)
  {
  case 0x08:
    return "Input";
  case 0x09:
    return "Output";
  case 0x0a:
    return "Collection";
  case 0x0b:
    return "Feature";
  case 0x0c:
    return "End Collection";
  default:
    return "Main";
  }
}

static const char *hidGlobalItemName(uint8_t tag)
{
  switch (tag)
  {
  case 0x00:
    return "Usage Page";
  case 0x01:
    return "Logical Minimum";
  case 0x02:
    return "Logical Maximum";
  case 0x03:
    return "Physical Minimum";
  case 0x04:
    return "Physical Maximum";
  case 0x05:
    return "Unit Exponent";
  case 0x06:
    return "Unit";
  case 0x07:
    return "Report Size";
  case 0x08:
    return "Report ID";
  case 0x09:
    return "Report Count";
  case 0x0a:
    return "Push";
  case 0x0b:
    return "Pop";
  default:
    return "Global";
  }
}

static const char *hidLocalItemName(uint8_t tag)
{
  switch (tag)
  {
  case 0x00:
    return "Usage";
  case 0x01:
    return "Usage Minimum";
  case 0x02:
    return "Usage Maximum";
  case 0x03:
    return "Designator Index";
  case 0x04:
    return "Designator Minimum";
  case 0x05:
    return "Designator Maximum";
  case 0x07:
    return "String Index";
  case 0x08:
    return "String Minimum";
  case 0x09:
    return "String Maximum";
  case 0x0a:
    return "Delimiter";
  default:
    return "Local";
  }
}

static const char *hidCollectionTypeName(uint32_t value)
{
  switch (value)
  {
  case 0x00:
    return "Physical";
  case 0x01:
    return "Application";
  case 0x02:
    return "Logical";
  case 0x03:
    return "Report";
  case 0x04:
    return "Named Array";
  case 0x05:
    return "Usage Switch";
  case 0x06:
    return "Usage Modifier";
  default:
    return "Vendor/Reserved";
  }
}

static void hidPrintMainItemFlags(uint32_t value, Print &out)
{
  out.print(value & (1 << 0) ? "Constant" : "Data");
  out.print(',');
  out.print(value & (1 << 1) ? "Variable" : "Array");
  out.print(',');
  out.print(value & (1 << 2) ? "Relative" : "Absolute");
  if (value & (1 << 3))
  {
    out.print(",Wrap");
  }
  if (value & (1 << 4))
  {
    out.print(",NonLinear");
  }
  if (value & (1 << 5))
  {
    out.print(",NoPreferred");
  }
  if (value & (1 << 6))
  {
    out.print(",NullState");
  }
  if (value & (1 << 8))
  {
    out.print(",Volatile");
  }
  if (value & (1 << 9))
  {
    out.print(",BufferedBytes");
  }
}

void espUsbHostPrintHIDReportDescriptor(const uint8_t *data, size_t length, Print &out)
{
  if (!data || length == 0)
  {
    out.println("HID report descriptor: empty");
    return;
  }

  out.printf("HID report descriptor: len=%u\n", static_cast<unsigned>(length));
  uint8_t indent = 0;
  for (size_t i = 0; i < length;)
  {
    const uint8_t prefix = data[i++];
    if (prefix == 0xfe)
    {
      if (i + 1 >= length)
      {
        out.println("  truncated long item");
        break;
      }
      const uint8_t itemLength = data[i++];
      const uint8_t longTag = data[i++];
      out.printf("  long item tag=0x%02x len=%u data=", longTag, itemLength);
      const size_t available = (i + itemLength <= length) ? itemLength : (length - i);
      espUsbHostPrintHex(&data[i], available, out);
      out.println();
      i += available;
      continue;
    }

    const size_t itemOffset = i - 1;
    const uint8_t sizeCode = prefix & 0x03;
    const size_t itemSize = sizeCode == 3 ? 4 : sizeCode;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag = (prefix >> 4) & 0x0f;
    const size_t available = (i + itemSize <= length) ? itemSize : (length - i);
    const int32_t value = hidItemValue(&data[i], available);
    const uint32_t unsignedValue = hidItemUnsignedValue(&data[i], available);
    const char *typeName = "Reserved";
    const char *itemName = "Reserved";
    if (type == 0)
    {
      typeName = "Main";
      itemName = hidMainItemName(tag);
    }
    else if (type == 1)
    {
      typeName = "Global";
      itemName = hidGlobalItemName(tag);
    }
    else if (type == 2)
    {
      typeName = "Local";
      itemName = hidLocalItemName(tag);
    }

    if (type == 0 && tag == 0x0c && indent > 0)
    {
      indent--;
    }

    out.printf("  %04u: 0x%02x ", static_cast<unsigned>(itemOffset), prefix);
    for (uint8_t level = 0; level < indent; level++)
    {
      out.print("  ");
    }
    out.printf("%-8s %-18s", typeName, itemName);
    if (itemSize > 0)
    {
      out.print(" value=");
      const bool signedValue = type == 1 && tag >= 0x01 && tag <= 0x05;
      if (signedValue)
      {
        out.print(value);
      }
      else
      {
        out.print(unsignedValue);
      }
      out.print(" raw=");
      espUsbHostPrintHex(&data[i], available, out);
    }
    if (type == 0 && tag == 0x0a)
    {
      out.print(" (");
      out.print(hidCollectionTypeName(unsignedValue));
      out.print(')');
    }
    else if (type == 0 && (tag == 0x08 || tag == 0x09 || tag == 0x0b) && itemSize > 0)
    {
      out.print(" (");
      hidPrintMainItemFlags(unsignedValue, out);
      out.print(')');
    }
    out.println();
    if (type == 0 && tag == 0x0a)
    {
      indent++;
    }
    i += available;
    if (available < itemSize)
    {
      out.println("  truncated item");
      break;
    }
  }
}

static uint32_t hidExtractUnsignedBits(const uint8_t *data, size_t length, uint16_t bitOffset, uint8_t bitSize)
{
  if (!data || bitSize == 0 || bitSize > 32)
  {
    return 0;
  }

  uint32_t value = 0;
  for (uint8_t bit = 0; bit < bitSize; bit++)
  {
    const size_t sourceBit = static_cast<size_t>(bitOffset) + bit;
    const size_t byteIndex = sourceBit / 8;
    if (byteIndex >= length)
    {
      break;
    }
    if (data[byteIndex] & (1U << (sourceBit % 8)))
    {
      value |= 1UL << bit;
    }
  }
  return value;
}

static int32_t hidSignExtend(uint32_t value, uint8_t bitSize)
{
  if (bitSize == 0 || bitSize >= 32)
  {
    return static_cast<int32_t>(value);
  }
  const uint32_t signBit = 1UL << (bitSize - 1);
  if ((value & signBit) == 0)
  {
    return static_cast<int32_t>(value);
  }
  const uint32_t extendMask = ~((1UL << bitSize) - 1);
  return static_cast<int32_t>(value | extendMask);
}

void espUsbHostPrint(const EspUsbHostHIDReportDescriptor &descriptor, Print &out)
{
  out.printf("hid report descriptor: address=%u iface=%u hid=0x%04x country=0x%02x type=0x%02x reported_len=%u len=%u\n",
             descriptor.address,
             descriptor.interfaceNumber,
             descriptor.hidVersion,
             descriptor.countryCode,
             descriptor.descriptorType,
             descriptor.reportedLength,
             descriptor.length);
  out.print("Raw HID report descriptor: ");
  espUsbHostPrintHex(descriptor.data, descriptor.length, out);
  out.println();
  espUsbHostPrintHIDReportDescriptor(descriptor.data, descriptor.length, out);
}

static void printHubPortStatus(EspUsbHost &usb, uint8_t hubAddress, uint8_t port, Print &out)
{
  uint16_t status = 0;
  uint16_t change = 0;
  if (!usb.getHubPortStatus(hubAddress, port, status, change))
  {
    out.printf("  Port %u status unavailable\n", port);
    return;
  }

  out.printf("  Port %u status=0x%04x change=0x%04x connected=%s enabled=%s suspended=%s over_current=%s reset=%s powered=%s low_speed=%s high_speed=%s test=%s indicator=%s\n",
             port,
             status,
             change,
             yesNo(status & 0x0001),
             yesNo(status & 0x0002),
             yesNo(status & 0x0004),
             yesNo(status & 0x0008),
             yesNo(status & 0x0010),
             yesNo(status & 0x0100),
             yesNo(status & 0x0200),
             yesNo(status & 0x0400),
             yesNo(status & 0x0800),
             yesNo(status & 0x1000));
}

static bool printHubInfo(EspUsbHost &usb, uint8_t hubAddress, bool printPorts, Print &out)
{
  EspUsbHostHubInfo hub;
  if (!usb.getHubInfo(hubAddress, hub))
  {
    out.printf("Hub address=%u descriptor unavailable\n", hubAddress);
    return false;
  }

  out.println("----------- USB Hub -----------");
  out.printf("Hub address=%u ports=%u descriptor_len=%u characteristics=0x%04x\n",
             hub.address,
             hub.portCount,
             hub.descriptorLength,
             hub.characteristics);
  out.printf("Power switching: per-port=%s ganged=%s none=%s\n",
             yesNo(hub.perPortPowerSwitching),
             yesNo(hub.gangedPowerSwitching),
             yesNo(hub.noPowerSwitching));
  out.printf("Over-current: per-port=%s ganged=%s none=%s\n",
             yesNo(hub.perPortOverCurrent),
             yesNo(hub.gangedOverCurrent),
             yesNo(hub.noOverCurrent));
  out.printf("Compound=%s power_on_to_good=%ums controller_current=%umA\n",
             yesNo(hub.compound),
             hub.powerOnToPowerGoodMs,
             hub.controllerCurrentMa);
  out.print("Raw hub descriptor: ");
  espUsbHostPrintHex(hub.rawDescriptor, hub.descriptorLength, out);
  out.println();

  if (printPorts)
  {
    for (uint8_t port = 1; port <= hub.portCount; port++)
    {
      printHubPortStatus(usb, hub.address, port, out);
    }
  }
  out.println("--------- USB Hub End ---------");
  return true;
}

static uint16_t ftdiBaudDivisor(uint32_t baud)
{
  static constexpr uint32_t FTDI_BASE_CLOCK = 48000000;
  static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
  if (baud == 0)
  {
    return 0;
  }
  uint32_t divisor3 = (FTDI_BASE_CLOCK + baud) / (2 * baud);
  uint32_t divisor = divisor3 >> 3;
  divisor |= static_cast<uint32_t>(divfrac[divisor3 & 0x7]) << 14;
  if (divisor == 1)
  {
    divisor = 0;
  }
  else if (divisor == 0x4001)
  {
    divisor = 1;
  }
  return static_cast<uint16_t>(divisor & 0xffff);
}

static bool isValidSerialConfig(const EspUsbHostSerialConfig &config)
{
  if (config.baud == 0 || config.dataBits < 5 || config.dataBits > 8)
  {
    return false;
  }
  return config.parity <= ESP_USB_HOST_SERIAL_PARITY_SPACE &&
         config.stopBits <= ESP_USB_HOST_SERIAL_STOP_BITS_2;
}

static uint8_t cdcStopBits(EspUsbHostSerialStopBits stopBits)
{
  return static_cast<uint8_t>(stopBits);
}

static uint8_t cdcParity(EspUsbHostSerialParity parity)
{
  return static_cast<uint8_t>(parity);
}

static void fillCdcLineCoding(const EspUsbHostSerialConfig &config, uint8_t lineCoding[7])
{
  lineCoding[0] = static_cast<uint8_t>(config.baud & 0xff);
  lineCoding[1] = static_cast<uint8_t>((config.baud >> 8) & 0xff);
  lineCoding[2] = static_cast<uint8_t>((config.baud >> 16) & 0xff);
  lineCoding[3] = static_cast<uint8_t>((config.baud >> 24) & 0xff);
  lineCoding[4] = cdcStopBits(config.stopBits);
  lineCoding[5] = cdcParity(config.parity);
  lineCoding[6] = config.dataBits;
}

static uint16_t ftdiDataCharacteristics(const EspUsbHostSerialConfig &config)
{
  uint16_t value = config.dataBits;
  value |= static_cast<uint16_t>(config.parity) << 8;
  switch (config.stopBits)
  {
  case ESP_USB_HOST_SERIAL_STOP_BITS_1_5:
    value |= 0x0800;
    break;
  case ESP_USB_HOST_SERIAL_STOP_BITS_2:
    value |= 0x1000;
    break;
  case ESP_USB_HOST_SERIAL_STOP_BITS_1:
  default:
    break;
  }
  return value;
}

static uint16_t cp210xLineControl(const EspUsbHostSerialConfig &config)
{
  uint16_t value = static_cast<uint16_t>(config.dataBits) << 8;
  switch (config.parity)
  {
  case ESP_USB_HOST_SERIAL_PARITY_ODD:
    value |= 0x0010;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_EVEN:
    value |= 0x0020;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_MARK:
    value |= 0x0030;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_SPACE:
    value |= 0x0040;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_NONE:
  default:
    break;
  }
  value |= static_cast<uint16_t>(config.stopBits);
  return value;
}

static uint16_t ch34xLineControl(const EspUsbHostSerialConfig &config)
{
  uint8_t lcr = 0xc0 | static_cast<uint8_t>(config.dataBits - 5);
  if (config.stopBits == ESP_USB_HOST_SERIAL_STOP_BITS_1_5 ||
      config.stopBits == ESP_USB_HOST_SERIAL_STOP_BITS_2)
  {
    lcr |= 0x04;
  }

  switch (config.parity)
  {
  case ESP_USB_HOST_SERIAL_PARITY_ODD:
    lcr |= 0x08;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_EVEN:
    lcr |= 0x18;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_MARK:
    lcr |= 0x28;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_SPACE:
    lcr |= 0x38;
    break;
  case ESP_USB_HOST_SERIAL_PARITY_NONE:
  default:
    break;
  }
  return lcr;
}

static uint32_t ch34xClockDiv(int prescaler, int factor)
{
  return 1UL << (12 - 3 * prescaler - factor);
}

static uint16_t ch34xBaudValue(uint32_t baud)
{
  static constexpr uint32_t CH34X_CLOCK_RATE = 48000000;
  uint32_t minRates[4];
  for (int i = 0; i < 4; i++)
  {
    minRates[i] = CH34X_CLOCK_RATE / (ch34xClockDiv(i, 1) * 512);
  }

  const uint32_t minBaud = (CH34X_CLOCK_RATE + ch34xClockDiv(0, 0) * 256 - 1) / (ch34xClockDiv(0, 0) * 256);
  const uint32_t maxBaud = CH34X_CLOCK_RATE / (ch34xClockDiv(3, 0) * 2);
  if (baud < minBaud)
  {
    baud = minBaud;
  }
  else if (baud > maxBaud)
  {
    baud = maxBaud;
  }

  int prescaler = -1;
  for (int i = 3; i >= 0; i--)
  {
    if (baud > minRates[i])
    {
      prescaler = i;
      break;
    }
  }
  if (prescaler < 0)
  {
    prescaler = 0;
  }

  int factor = 1;
  uint32_t clockDiv = ch34xClockDiv(prescaler, factor);
  uint32_t divisor = CH34X_CLOCK_RATE / (clockDiv * baud);
  if (divisor < 9 || divisor > 255)
  {
    divisor /= 2;
    clockDiv *= 2;
    factor = 0;
  }

  if (divisor < 2)
  {
    divisor = 2;
  }
  else if (divisor > 255)
  {
    divisor = 255;
  }

  const uint32_t lowerError = 16 * CH34X_CLOCK_RATE / (clockDiv * divisor) - 16 * baud;
  const uint32_t higherError = 16 * baud - 16 * CH34X_CLOCK_RATE / (clockDiv * (divisor + 1));
  if (lowerError >= higherError && divisor < 255)
  {
    divisor++;
  }
  if (factor == 1 && divisor % 2 == 0)
  {
    divisor /= 2;
    factor = 0;
  }

  return static_cast<uint16_t>(((0x100 - divisor) << 8) | (factor << 2) | prescaler);
}

struct HubControlTransferContext
{
  volatile bool done = false;
  usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
};

static void hubControlTransferCallback(usb_transfer_t *transfer)
{
  HubControlTransferContext *context = static_cast<HubControlTransferContext *>(transfer->context);
  if (context)
  {
    context->status = transfer->status;
    context->done = true;
  }
}

EspUsbHost::EspUsbHost() = default;

EspUsbHost::~EspUsbHost()
{
  end();
}

bool EspUsbHost::begin()
{
  return begin(EspUsbHostConfig());
}

bool EspUsbHost::begin(const EspUsbHostConfig &config)
{
  if (running_)
  {
    ESP_LOGW(TAG, "begin() called while USB Host is already running");
    return true;
  }

  config_ = config;
  running_ = true;
  ready_ = false;
  lastError_ = ESP_OK;
  nextHubIndex_ = 1;

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY)
  {
    created = xTaskCreate(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_);
  }
  else
  {
    created = xTaskCreatePinnedToCore(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_, config_.taskCore);
  }

  if (created != pdPASS)
  {
    running_ = false;
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  const uint32_t start = millis();
  while (running_ && !ready_ && millis() - start < 1000)
  {
    delay(1);
  }
  return ready_;
}

void EspUsbHost::end()
{
  if (!running_)
  {
    return;
  }

  ESP_LOGI(TAG, "Stopping USB Host");
  running_ = false;
  if (clientTaskHandle_)
  {
    for (int i = 0; i < 200 && clientTaskHandle_; i++)
    {
      delay(1);
    }
    if (clientTaskHandle_)
    {
      vTaskDelete(clientTaskHandle_);
      clientTaskHandle_ = nullptr;
    }
  }
  if (taskHandle_)
  {
    for (int i = 0; i < 200 && taskHandle_; i++)
    {
      delay(1);
    }
    if (taskHandle_)
    {
      vTaskDelete(taskHandle_);
      taskHandle_ = nullptr;
    }
  }
  ready_ = false;
}

bool EspUsbHost::ready() const
{
  return ready_;
}

void EspUsbHost::onDeviceConnected(DeviceCallback callback)
{
  deviceConnectedCallback_ = callback;
}

void EspUsbHost::onDeviceDisconnected(DeviceCallback callback)
{
  deviceDisconnectedCallback_ = callback;
}

void EspUsbHost::onKeyboard(KeyboardCallback callback)
{
  keyboardCallback_ = callback;
}

void EspUsbHost::onMouse(MouseCallback callback)
{
  mouseCallback_ = callback;
}

void EspUsbHost::onHIDInput(HIDInputCallback callback)
{
  hidInputCallback_ = callback;
}

void EspUsbHost::onHIDReportDescriptor(HIDReportDescriptorCallback callback)
{
  hidReportDescriptorCallback_ = callback;
}

void EspUsbHost::onSerialData(SerialDataCallback callback)
{
  serialDataCallback_ = callback;
}

void EspUsbHost::onMidiMessage(MidiMessageCallback callback)
{
  midiMessageCallback_ = callback;
}

void EspUsbHost::onAudioData(AudioDataCallback callback)
{
  audioDataCallback_ = callback;
}

void EspUsbHost::onAudioOutputRequest(AudioOutputCallback callback)
{
  audioOutputCallback_ = callback;
}

void EspUsbHost::onConsumerControl(ConsumerControlCallback callback)
{
  consumerControlCallback_ = callback;
}

void EspUsbHost::onGamepad(GamepadCallback callback)
{
  gamepadCallback_ = callback;
}

void EspUsbHost::onHIDVendorInput(HIDVendorInputCallback callback)
{
  hidVendorInputCallback_ = callback;
}

void EspUsbHost::onVendorData(VendorDataCallback callback)
{
  vendorDataCallback_ = callback;
}

void EspUsbHost::onSystemControl(SystemControlCallback callback)
{
  systemControlCallback_ = callback;
}

void EspUsbHost::setKeyboardLayout(EspUsbHostKeyboardLayout layout)
{
  keyboardLayout_ = layout;
}

bool EspUsbHost::sendHIDReport(uint8_t interfaceNumber,
                               uint8_t reportType,
                               uint8_t reportId,
                               const uint8_t *data,
                               size_t length,
                               uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!running_ || !device || !device->handle || !clientHandle_)
  {
    ESP_LOGW(TAG, "sendHIDReport() called while no HID device is open");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendHIDReport() called with null data");
    return false;
  }
  if (reportType != ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT &&
      reportType != ESP_USB_HOST_HID_REPORT_TYPE_FEATURE)
  {
    ESP_LOGW(TAG, "sendHIDReport() unsupported reportType=%u", reportType);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(control) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = HID_SET_REPORT_REQUEST_TYPE;
  setup->bRequest = HID_CLASS_REQUEST_SET_REPORT;
  setup->wValue = (static_cast<uint16_t>(reportType) << 8) | reportId;
  setup->wIndex = interfaceNumber;
  setup->wLength = length;
  if (length > 0)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Set_Report) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "HID Set_Report submitted iface=%u type=%u id=%u length=%u",
           interfaceNumber,
           reportType,
           reportId,
           static_cast<unsigned>(length));
  return true;
}

bool EspUsbHost::setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address)
{
  DeviceState *device = findKeyboardDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "setKeyboardLeds() called before a keyboard interface is ready");
    return false;
  }

  device->keyboardNumLock = numLock;
  device->keyboardCapsLock = capsLock;
  device->keyboardScrollLock = scrollLock;
  uint8_t leds = espUsbHostBuildKeyboardLedReport(numLock, capsLock, scrollLock);
  return sendKeyboardLedReport(*device, leds);
}

bool EspUsbHost::getKeyboardNumLock(uint8_t address) const
{
  const DeviceState *device = findKeyboardDevice(address);
  return device ? device->keyboardNumLock : false;
}

bool EspUsbHost::getKeyboardCapsLock(uint8_t address) const
{
  const DeviceState *device = findKeyboardDevice(address);
  return device ? device->keyboardCapsLock : false;
}

bool EspUsbHost::getKeyboardScrollLock(uint8_t address) const
{
  const DeviceState *device = findKeyboardDevice(address);
  return device ? device->keyboardScrollLock : false;
}

bool EspUsbHost::setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable)
{
  if (!running_ || !clientHandle_)
  {
    ESP_LOGW(TAG, "setHubPortPower() called before USB Host is ready");
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "setHubPortPower() cannot run from USB client task");
    return false;
  }
  if (hubAddress == 0 || port == 0)
  {
    ESP_LOGW(TAG, "setHubPortPower() invalid hubAddress=%u port=%u", hubAddress, port);
    return false;
  }

  usb_device_handle_t hubHandle = nullptr;
  DeviceState *knownHub = findDevice(hubAddress);
  const bool openedTemporarily = !(knownHub && knownHub->handle);
  if (openedTemporarily)
  {
    esp_err_t err = usb_host_device_open(clientHandle_, hubAddress, &hubHandle);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_device_open(hub=%u) failed: %s", hubAddress, esp_err_to_name(err));
      setLastError(err);
      return false;
    }
  }
  else
  {
    hubHandle = knownHub->handle;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(hub control) failed: %s", esp_err_to_name(err));
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  HubControlTransferContext *context = new HubControlTransferContext();
  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_HUB_PORT_REQUEST_TYPE;
  setup->bRequest = enable ? USB_REQUEST_SET_FEATURE : USB_REQUEST_CLEAR_FEATURE;
  setup->wValue = USB_HUB_FEATURE_PORT_POWER;
  setup->wIndex = port;
  setup->wLength = 0;

  transfer->device_handle = hubHandle;
  transfer->bEndpointAddress = 0;
  transfer->callback = hubControlTransferCallback;
  transfer->context = context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(HUB port power) failed: %s", esp_err_to_name(err));
    usb_host_transfer_free(transfer);
    delete context;
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  const uint32_t deadline = millis() + 1000;
  while (!context->done && millis() < deadline)
  {
    delay(1);
  }

  const bool done = context->done;
  const usb_transfer_status_t transferStatus = context->status;
  const bool ok = done && transferStatus == USB_TRANSFER_STATUS_COMPLETED;
  if (!done)
  {
    ESP_LOGW(TAG, "HUB port power request timed out hub=%u port=%u enable=%u", hubAddress, port, enable ? 1 : 0);
    setLastError(ESP_ERR_TIMEOUT);
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    return false;
  }
  else if (!ok)
  {
    ESP_LOGW(TAG, "HUB port power request failed status=%d hub=%u port=%u enable=%u",
             transferStatus,
             hubAddress,
             port,
             enable ? 1 : 0);
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  delete context;
  if (openedTemporarily)
  {
    usb_host_device_close(clientHandle_, hubHandle);
  }
  return ok;
}

bool EspUsbHost::getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change)
{
  status = 0;
  change = 0;
  if (!running_ || !clientHandle_)
  {
    ESP_LOGW(TAG, "getHubPortStatus() called before USB Host is ready");
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "getHubPortStatus() cannot run from USB client task");
    return false;
  }
  if (hubAddress == 0 || port == 0)
  {
    ESP_LOGW(TAG, "getHubPortStatus() invalid hubAddress=%u port=%u", hubAddress, port);
    return false;
  }

  usb_device_handle_t hubHandle = nullptr;
  DeviceState *knownHub = findDevice(hubAddress);
  const bool openedTemporarily = !(knownHub && knownHub->handle);
  if (openedTemporarily)
  {
    esp_err_t err = usb_host_device_open(clientHandle_, hubAddress, &hubHandle);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_device_open(hub=%u) failed: %s", hubAddress, esp_err_to_name(err));
      setLastError(err);
      return false;
    }
  }
  else
  {
    hubHandle = knownHub->handle;
  }

  static constexpr size_t STATUS_LENGTH = 4;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + STATUS_LENGTH, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(hub status) failed: %s", esp_err_to_name(err));
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  HubControlTransferContext *context = new HubControlTransferContext();
  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_HUB_PORT_IN_REQUEST_TYPE;
  setup->bRequest = USB_REQUEST_GET_STATUS;
  setup->wValue = 0;
  setup->wIndex = port;
  setup->wLength = STATUS_LENGTH;

  transfer->device_handle = hubHandle;
  transfer->bEndpointAddress = 0;
  transfer->callback = hubControlTransferCallback;
  transfer->context = context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + STATUS_LENGTH;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(HUB port status) failed: %s", esp_err_to_name(err));
    usb_host_transfer_free(transfer);
    delete context;
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  const uint32_t deadline = millis() + 1000;
  while (!context->done && millis() < deadline)
  {
    delay(1);
  }

  const bool done = context->done;
  const usb_transfer_status_t transferStatus = context->status;
  const bool ok = done && transferStatus == USB_TRANSFER_STATUS_COMPLETED;
  if (ok)
  {
    const uint8_t *data = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
    status = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
    change = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
  }
  else if (!done)
  {
    ESP_LOGW(TAG, "HUB port status request timed out hub=%u port=%u", hubAddress, port);
    setLastError(ESP_ERR_TIMEOUT);
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    return false;
  }
  else
  {
    ESP_LOGW(TAG, "HUB port status request failed status=%d hub=%u port=%u",
             transferStatus,
             hubAddress,
             port);
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  delete context;
  if (openedTemporarily)
  {
    usb_host_device_close(clientHandle_, hubHandle);
  }
  return ok;
}

bool EspUsbHost::sendSetProtocol(uint8_t interfaceNumber, uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!running_ || !device || !device->handle || !clientHandle_)
  {
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = HID_SET_REPORT_REQUEST_TYPE;
  setup->bRequest = HID_CLASS_REQUEST_SET_PROTOCOL;
  setup->wValue = HID_PROTOCOL_REPORT_MODE;
  setup->wIndex = interfaceNumber;
  setup->wLength = 0;

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Set_Protocol iface=%u) failed: %s",
             interfaceNumber,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::sendKeyboardLedReport(DeviceState &device, uint8_t leds)
{
  if (device.keyboardLedPending)
  {
    return false;
  }
  if (sendHIDReport(device.keyboardInterfaceNumber,
                    ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                    0,
                    &leds,
                    sizeof(leds),
                    device.info.address))
  {
    device.keyboardLedPending = true;
    device.keyboardLedLastSent = leds;
    return true;
  }
  return false;
}

bool EspUsbHost::sendHIDVendorOutput(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findHIDVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "sendHIDVendorOutput() called before a vendor HID interface is ready");
    return false;
  }
  if (!device->hasVendorOutEndpoint)
  {
    ESP_LOGW(TAG, "sendHIDVendorOutput() no interrupt OUT endpoint");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendHIDVendorOutput() called with null data");
    return false;
  }

  const size_t packetSize = length > device->vendorOutPacketSize ? length : device->vendorOutPacketSize;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->vendorOutEndpointAddress;
  transfer->callback = outputTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(vendor OUT ep=0x%02x) failed: %s",
             device->vendorOutEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::sendHIDVendorFeature(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findHIDVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "sendHIDVendorFeature() called before a vendor HID interface is ready");
    return false;
  }
  return sendHIDReport(device->vendorInterfaceNumber,
                       ESP_USB_HOST_HID_REPORT_TYPE_FEATURE,
                       ESP_USB_HOST_HID_REPORT_ID_VENDOR,
                       data,
                       length,
                       device->info.address);
}

bool EspUsbHost::vendorOpen(uint8_t address, uint8_t interfaceNumber)
{
  DeviceState *device = findUsbVendorCandidate(address, interfaceNumber);
  if (!device)
  {
    ESP_LOGW(TAG, "vendorOpen() no vendor-specific bulk interface");
    return false;
  }

  uint8_t selectedInterface = interfaceNumber;
  EspUsbHostEndpointInfo inEndpoint;
  EspUsbHostEndpointInfo outEndpoint;
  bool found = false;

  for (uint8_t i = 0; i < device->interfaceInfoCount; i++)
  {
    const EspUsbHostInterfaceInfo &intf = device->interfaceInfos[i];
    if (intf.interfaceClass != USB_CLASS_VENDOR_VALUE)
    {
      continue;
    }
    if (interfaceNumber != 0xff && intf.number != interfaceNumber)
    {
      continue;
    }

    bool hasIn = false;
    bool hasOut = false;
    EspUsbHostEndpointInfo candidateIn;
    EspUsbHostEndpointInfo candidateOut;
    for (uint8_t e = 0; e < device->endpointInfoCount; e++)
    {
      const EspUsbHostEndpointInfo &ep = device->endpointInfos[e];
      const bool isBulk = (ep.attributes & 0x03) == 0x02;
      if (ep.interfaceNumber != intf.number || !isBulk)
      {
        continue;
      }
      if (ep.address & 0x80)
      {
        candidateIn = ep;
        hasIn = true;
      }
      else
      {
        candidateOut = ep;
        hasOut = true;
      }
    }

    if (hasIn && hasOut)
    {
      selectedInterface = intf.number;
      inEndpoint = candidateIn;
      outEndpoint = candidateOut;
      found = true;
      break;
    }
  }

  if (!found)
  {
    ESP_LOGW(TAG, "vendorOpen() no bulk IN/OUT pair");
    return false;
  }

  if (!device->hasUsbVendorInterface)
  {
    esp_err_t err = usb_host_interface_claim(clientHandle_, device->handle, selectedInterface, 0);
    for (uint8_t i = 0; i < device->interfaceInfoCount; i++)
    {
      EspUsbHostInterfaceInfo &info = device->interfaceInfos[i];
      if (info.number == selectedInterface)
      {
        info.claimAttempted = true;
        info.claimResult = err;
        if (err == ESP_OK)
        {
          info.claimed = true;
        }
        break;
      }
    }
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_interface_claim(vendor iface=%u) failed: %s",
               selectedInterface,
               esp_err_to_name(err));
      setLastError(err);
      return false;
    }
    if (device->interfaceCount < sizeof(device->interfaces))
    {
      device->interfaces[device->interfaceCount++] = selectedInterface;
    }
    device->endpointChannelCount = static_cast<uint8_t>(device->endpointChannelCount + 2);
  }
  else if (device->usbVendorInterfaceNumber != selectedInterface)
  {
    ESP_LOGW(TAG, "vendorOpen() another vendor interface is already open");
    return false;
  }

  device->hasUsbVendorInterface = true;
  device->usbVendorInterfaceNumber = selectedInterface;
  device->hasUsbVendorInEndpoint = true;
  device->usbVendorInEndpointAddress = inEndpoint.address;
  device->usbVendorInPacketSize = inEndpoint.maxPacketSize;
  device->hasUsbVendorOutEndpoint = true;
  device->usbVendorOutEndpointAddress = outEndpoint.address;
  device->usbVendorOutPacketSize = outEndpoint.maxPacketSize;

  EndpointState *existingEndpoint = findEndpoint(device->handle, inEndpoint.address);
  if (!existingEndpoint)
  {
    EndpointState *endpoint = allocateEndpoint(*device);
    if (!endpoint)
    {
      ESP_LOGW(TAG, "No endpoint slots available for vendor IN");
      setLastError(ESP_ERR_NO_MEM);
      return false;
    }

    esp_err_t err = usb_host_transfer_alloc(inEndpoint.maxPacketSize, 0, &endpoint->transfer);
    if (err != ESP_OK)
    {
      endpoint->inUse = false;
      ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor IN) failed: %s", esp_err_to_name(err));
      setLastError(err);
      return false;
    }

    endpoint->address = inEndpoint.address;
    endpoint->interfaceNumber = selectedInterface;
    endpoint->alternate = 0;
    endpoint->interfaceClass = USB_CLASS_VENDOR_VALUE;
    endpoint->interfaceSubClass = 0;
    endpoint->interfaceProtocol = 0;
    endpoint->transfer->device_handle = device->handle;
    endpoint->transfer->bEndpointAddress = inEndpoint.address;
    endpoint->transfer->callback = transferCallback;
    endpoint->transfer->context = this;
    endpoint->transfer->num_bytes = inEndpoint.maxPacketSize;

    if (!submitInputTransfer(*endpoint))
    {
      return false;
    }
  }

  ESP_LOGI(TAG, "USB vendor bulk interface ready: address=%u iface=%u in=0x%02x out=0x%02x",
           device->info.address,
           selectedInterface,
           inEndpoint.address,
           outEndpoint.address);
  return true;
}

bool EspUsbHost::vendorWrite(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "vendorWrite() called before vendorOpen()");
    return false;
  }
  if (!device->hasUsbVendorOutEndpoint)
  {
    ESP_LOGW(TAG, "vendorWrite() no bulk OUT endpoint");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "vendorWrite() called with null data");
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  const size_t packetSize = length > device->usbVendorOutPacketSize ? length : device->usbVendorOutPacketSize;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor bulk OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->usbVendorOutEndpointAddress;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(vendor bulk OUT ep=0x%02x) failed: %s",
             device->usbVendorOutEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(1000)) == pdTRUE;
  const bool ok = done && context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (!done)
  {
    ESP_LOGW(TAG, "USB vendor bulk OUT timeout ep=0x%02x", device->usbVendorOutEndpointAddress);
    setLastError(ESP_ERR_TIMEOUT);
  }
  else if (!ok)
  {
    ESP_LOGW(TAG, "USB vendor bulk OUT failed ep=0x%02x status=%d actual=%u",
             device->usbVendorOutEndpointAddress,
             context.status,
             static_cast<unsigned>(context.actualLength));
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  return ok;
}

size_t EspUsbHost::vendorRead(uint8_t *buffer, size_t length, uint8_t address)
{
  if (!buffer || length == 0)
  {
    return 0;
  }
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    return 0;
  }

  size_t copied = 0;
  while (copied < length && device->usbVendorRxCount > 0)
  {
    buffer[copied++] = device->usbVendorRxBuffer[device->usbVendorRxTail];
    device->usbVendorRxTail = (device->usbVendorRxTail + 1) % ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE;
    device->usbVendorRxCount--;
  }
  return copied;
}

bool EspUsbHost::vendorControlIn(uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 uint8_t *data,
                                 size_t length,
                                 size_t *actualLength,
                                 uint8_t address,
                                 uint32_t timeoutMs)
{
  if (length > 0 && !data)
  {
    return false;
  }
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    device = findDevice(address);
  }
  if (!device || !device->handle)
  {
    return false;
  }
  return submitVendorControl(*device, VENDOR_IN_REQUEST_TYPE, request, value, index, data, length, actualLength, timeoutMs);
}

bool EspUsbHost::vendorControlOut(uint8_t request,
                                  uint16_t value,
                                  uint16_t index,
                                  const uint8_t *data,
                                  size_t length,
                                  uint8_t address,
                                  uint32_t timeoutMs)
{
  if (length > 0 && !data)
  {
    return false;
  }
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    device = findDevice(address);
  }
  if (!device || !device->handle)
  {
    return false;
  }
  return submitVendorControl(*device,
                             VENDOR_OUT_REQUEST_TYPE,
                             request,
                             value,
                             index,
                             const_cast<uint8_t *>(data),
                             length,
                             nullptr,
                             timeoutMs);
}

bool EspUsbHost::sendSerial(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "sendSerial() called before a CDC OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendSerial() called with null data");
    return false;
  }

  const size_t packetSize = length > device->serialOutPacketSize ? length : device->serialOutPacketSize;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(serial OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->serialOutEndpointAddress;
  transfer->callback = serialOutTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(serial OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::sendSerial(const char *text, uint8_t address)
{
  if (!text)
  {
    return false;
  }
  return sendSerial(reinterpret_cast<const uint8_t *>(text), strlen(text), address);
}

bool EspUsbHost::serialReady(uint8_t address) const
{
  return findSerialDevice(address) != nullptr;
}

bool EspUsbHost::setSerialBaudRate(uint32_t baud, uint8_t address)
{
  EspUsbHostSerialConfig config = defaultSerialConfig_;
  DeviceState *device = findSerialDevice(address);
  if (device)
  {
    config = device->serialConfig;
  }
  config.baud = baud;
  return setSerialConfig(config, address);
}

bool EspUsbHost::setSerialConfig(const EspUsbHostSerialConfig &config, uint8_t address)
{
  if (!isValidSerialConfig(config))
  {
    ESP_LOGW(TAG, "setSerialConfig() called with invalid config: baud=%lu dataBits=%u parity=%u stopBits=%u",
             static_cast<unsigned long>(config.baud),
             config.dataBits,
             static_cast<unsigned>(config.parity),
             static_cast<unsigned>(config.stopBits));
    return false;
  }

  DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    if (address == ESP_USB_HOST_ANY_ADDRESS)
    {
      defaultSerialConfig_ = config;
    }
    return true;
  }
  if (address == ESP_USB_HOST_ANY_ADDRESS)
  {
    defaultSerialConfig_ = config;
  }

  device->serialConfig = config;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    configureVendorSerial(*device);
  }
  return true;
}

bool EspUsbHost::midiReady(uint8_t address) const
{
  return findMidiDevice(address) != nullptr;
}

bool EspUsbHost::audioInputReady(uint8_t address) const
{
  return findAudioInputDevice(address) != nullptr;
}

bool EspUsbHost::audioOutputReady(uint8_t address) const
{
  return findAudioOutputDevice(address) != nullptr;
}

bool EspUsbHost::audioInputStart(uint8_t channels,
                                 uint8_t bitsPerSample,
                                 uint32_t sampleRate,
                                 uint8_t address)
{
  if (channels == 0 || bitsPerSample == 0 || sampleRate == 0)
  {
    ESP_LOGW(TAG, "audioInputStart() called with incomplete audio IN format");
    return false;
  }

  DeviceState *device = findAudioInputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioInputStart() called before a USB Audio IN endpoint is ready");
    return false;
  }

  for (uint8_t i = 0; i < device->audioStreamInfoCount; i++)
  {
    const EspUsbHostAudioStreamInfo &stream = device->audioStreamInfos[i];
    if (stream.input &&
        stream.channels == channels &&
        stream.bitsPerSample == bitsPerSample &&
        espUsbHostAudioStreamSupportsSampleRate(stream, sampleRate))
    {
      return audioInputStart(stream, sampleRate, device->info.address);
    }
  }

  ESP_LOGW(TAG, "No matching USB Audio IN stream: channels=%u bits=%u rate=%lu",
           channels,
           bitsPerSample,
           static_cast<unsigned long>(sampleRate));
  return false;
}

bool EspUsbHost::audioInputStart(const EspUsbHostAudioStreamInfo &stream,
                                 uint32_t sampleRate,
                                 uint8_t address)
{
  if (!stream.input)
  {
    ESP_LOGW(TAG, "audioInputStart() called with a non-input stream");
    return false;
  }

  DeviceState *device = findAudioInputDevice(address == ESP_USB_HOST_ANY_ADDRESS ? stream.address : address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioInputStart() called before a USB Audio IN endpoint is ready");
    return false;
  }

  const uint32_t selectedRate = espUsbHostAudioStreamPreferredSampleRate(stream, sampleRate);
  if (selectedRate == 0)
  {
    ESP_LOGW(TAG, "audioInputStart() called with unsupported sampleRate=%lu",
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  EndpointState *selectedEndpoint = nullptr;
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse ||
        endpoint.deviceHandle != device->handle ||
        endpoint.interfaceClass != USB_CLASS_AUDIO_VALUE ||
        endpoint.interfaceSubClass != USB_AUDIO_SUBCLASS_AUDIO_STREAMING ||
        endpoint.interfaceNumber != stream.interfaceNumber ||
        endpoint.alternate != stream.alternate ||
        endpoint.address != stream.endpointAddress)
    {
      continue;
    }
    selectedEndpoint = &endpoint;
    break;
  }

  if (!selectedEndpoint)
  {
    ESP_LOGW(TAG, "No endpoint for USB Audio IN stream: iface=%u alt=%u ep=0x%02x",
             stream.interfaceNumber,
             stream.alternate,
             stream.endpointAddress);
    return false;
  }

  device->audioSampleRate = selectedRate;
  device->audioInInterfaceNumber = stream.interfaceNumber;
  device->audioInAlternate = stream.alternate;
  device->audioInEndpointAddress = stream.endpointAddress;
  device->audioInChannels = stream.channels;
  device->audioInBytesPerSample = stream.bytesPerSample;
  device->audioInBitsPerSample = stream.bitsPerSample;

  bool submitted = submitAudioSamplingFrequency(*device, stream.endpointAddress, selectedRate);
  if (stream.alternate == 0)
  {
    submitted = submitInputTransfer(*selectedEndpoint) && submitted;
  }
  else
  {
    submitted = submitSetInterface(*device, stream.interfaceNumber, stream.alternate) && submitted;
  }
  return submitted;
}

bool EspUsbHost::mscReady(uint8_t address) const
{
  return findMscDevice(address) != nullptr;
}

bool EspUsbHost::mscLastSense(EspUsbHostMscSense &sense, uint8_t address) const
{
  const DeviceState *device = findMscDevice(address);
  if (!device || !device->hasMscLastSense)
  {
    return false;
  }
  sense = device->mscLastSense;
  return true;
}

bool EspUsbHost::mscMaxLun(uint8_t &maxLun, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscMaxLun() called before a USB MSC device is ready");
    return false;
  }
  if (device->hasMscMaxLun)
  {
    maxLun = device->mscMaxLun;
    return true;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "mscMaxLun() cannot run from USB client task");
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + 1, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(MSC Get Max LUN) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_MSC_GET_MAX_LUN_REQUEST_TYPE;
  setup->bRequest = USB_MSC_GET_MAX_LUN_REQUEST;
  setup->wValue = 0;
  setup->wIndex = device->mscInterfaceNumber;
  setup->wLength = 1;

  context.status = USB_TRANSFER_STATUS_ERROR;
  context.actualLength = 0;
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + 1;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(MSC Get Max LUN) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  bool ok = done &&
            context.status == USB_TRANSFER_STATUS_COMPLETED &&
            context.actualLength >= USB_SETUP_PACKET_SIZE + 1;
  if (ok)
  {
    maxLun = transfer->data_buffer[USB_SETUP_PACKET_SIZE];
  }
  else
  {
    if (!done)
    {
      ESP_LOGW(TAG, "MSC Get Max LUN timeout");
      setLastError(ESP_ERR_TIMEOUT);
      usb_host_transfer_free(transfer);
      vSemaphoreDelete(context.done);
      return false;
    }
    ESP_LOGW(TAG, "MSC Get Max LUN failed status=%d actual=%u, assuming LUN 0",
             context.status,
             static_cast<unsigned>(context.actualLength));
    maxLun = 0;
    ok = true;
  }

  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  device->mscMaxLun = maxLun;
  device->hasMscMaxLun = true;
  return ok;
}

bool EspUsbHost::mscSelectLun(uint8_t lun, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscSelectLun() called before a USB MSC device is ready");
    return false;
  }

  uint8_t maxLun = 0;
  if (!mscMaxLun(maxLun, device->info.address, timeoutMs))
  {
    return false;
  }
  if (lun > maxLun)
  {
    ESP_LOGW(TAG, "mscSelectLun() invalid LUN %u max=%u", lun, maxLun);
    return false;
  }
  if (device->mscLun == lun)
  {
    return true;
  }

  device->mscLun = lun;
  device->mscBlockCount = 0;
  device->mscBlockCount64 = 0;
  device->mscBlockSize = 0;
  device->hasMscLastSense = false;
  return true;
}

bool EspUsbHost::mscGetBlockDeviceInfo(EspUsbHostMscBlockDeviceInfo &info, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscGetBlockDeviceInfo() called before a USB MSC device is ready");
    return false;
  }

  uint8_t maxLun = 0;
  if (!mscMaxLun(maxLun, device->info.address, timeoutMs))
  {
    return false;
  }

  uint64_t blockCount = device->mscBlockCount64;
  uint32_t blockSize = device->mscBlockSize;
  if (blockCount == 0 || blockSize == 0)
  {
    if (!mscCapacity64(blockCount, blockSize, device->info.address, timeoutMs))
    {
      return false;
    }
  }

  info = EspUsbHostMscBlockDeviceInfo();
  info.address = device->info.address;
  info.interfaceNumber = device->mscInterfaceNumber;
  info.lun = device->mscLun;
  info.maxLun = maxLun;
  info.blockCount = blockCount;
  info.blockSize = blockSize;
  info.capacityBytes = blockCount * static_cast<uint64_t>(blockSize);
  return true;
}

bool EspUsbHost::setAudioSampleRate(uint32_t sampleRate, uint8_t address)
{
  if (sampleRate == 0 || sampleRate > 0x00ffffff)
  {
    ESP_LOGW(TAG, "setAudioSampleRate() called with invalid sampleRate=%lu",
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  if (address == ESP_USB_HOST_ANY_ADDRESS)
  {
    defaultAudioSampleRate_ = sampleRate;
  }

  DeviceState *device = findDevice(address);
  if (!device)
  {
    return true;
  }

  device->audioSampleRate = sampleRate;
  bool submitted = true;
  if (device->hasAudioOutEndpoint)
  {
    submitted = submitAudioSamplingFrequency(*device, device->audioOutEndpointAddress, device->audioSampleRate) && submitted;
  }
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse ||
        endpoint.deviceHandle != device->handle ||
        endpoint.interfaceClass != USB_CLASS_AUDIO_VALUE ||
        endpoint.interfaceSubClass != USB_AUDIO_SUBCLASS_AUDIO_STREAMING ||
        (endpoint.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0)
    {
      continue;
    }
    submitted = submitAudioSamplingFrequency(*device, endpoint.address, device->audioSampleRate) && submitted;
  }
  return submitted;
}

bool EspUsbHost::audioOutputStart(uint8_t channels,
                                  uint8_t bitsPerSample,
                                  uint32_t sampleRate,
                                  uint8_t address)
{
  if (channels == 0 || bitsPerSample == 0 || sampleRate == 0)
  {
    ESP_LOGW(TAG, "audioOutputStart() called with incomplete audio OUT format");
    return false;
  }

  DeviceState *device = findAudioOutputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioOutputStart() called before a USB Audio OUT endpoint is ready");
    return false;
  }

  for (uint8_t i = 0; i < device->audioStreamInfoCount; i++)
  {
    const EspUsbHostAudioStreamInfo &stream = device->audioStreamInfos[i];
    if (stream.output &&
        stream.channels == channels &&
        stream.bitsPerSample == bitsPerSample &&
        espUsbHostAudioStreamSupportsSampleRate(stream, sampleRate))
    {
      return audioOutputStart(stream, sampleRate, device->info.address);
    }
  }

  ESP_LOGW(TAG, "No matching USB Audio OUT stream: channels=%u bits=%u rate=%lu",
           channels,
           bitsPerSample,
           static_cast<unsigned long>(sampleRate));
  return false;
}

bool EspUsbHost::audioOutputStart(const EspUsbHostAudioStreamInfo &stream,
                                  uint32_t sampleRate,
                                  uint8_t address)
{
  if (!stream.output)
  {
    ESP_LOGW(TAG, "audioOutputStart() called with a non-output stream");
    return false;
  }

  DeviceState *device = findAudioOutputDevice(address == ESP_USB_HOST_ANY_ADDRESS ? stream.address : address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioOutputStart() called before a USB Audio OUT endpoint is ready");
    return false;
  }
  if (device->audioOutRunning)
  {
    return true;
  }
  for (size_t i = 0; i < ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS; i++)
  {
    if (device->audioOutTransfers[i])
    {
      ESP_LOGW(TAG, "audioOutputStart() called while previous audio OUT transfers are stopping");
      return false;
    }
  }

  const uint32_t selectedRate = espUsbHostAudioStreamPreferredSampleRate(stream, sampleRate);
  if (selectedRate == 0)
  {
    ESP_LOGW(TAG, "audioOutputStart() called with unsupported sampleRate=%lu",
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  bool hasEndpoint = false;
  for (const EspUsbHostAudioStreamInfo &candidate : device->audioStreamInfos)
  {
    if (candidate.output &&
        candidate.interfaceNumber == stream.interfaceNumber &&
        candidate.alternate == stream.alternate &&
        candidate.endpointAddress == stream.endpointAddress)
    {
      hasEndpoint = true;
      break;
    }
  }
  if (!hasEndpoint)
  {
    ESP_LOGW(TAG, "No endpoint for USB Audio OUT stream: iface=%u alt=%u ep=0x%02x",
             stream.interfaceNumber,
             stream.alternate,
             stream.endpointAddress);
    return false;
  }

  device->audioSampleRate = selectedRate;
  device->audioOutInterfaceNumber = stream.interfaceNumber;
  device->audioOutEndpointAddress = stream.endpointAddress;
  device->audioOutPacketSize = stream.maxPacketSize;
  device->audioOutChannels = stream.channels;
  device->audioOutBytesPerSample = stream.bytesPerSample;
  device->audioOutBitsPerSample = stream.bitsPerSample;
  device->audioOutInterval = stream.interval;

  if (device->audioOutPacketSize == 0 ||
      device->audioOutChannels == 0 ||
      device->audioOutBytesPerSample == 0 ||
      device->audioSampleRate == 0)
  {
    ESP_LOGW(TAG, "audioOutputStart() called with incomplete audio OUT format");
    return false;
  }

  bool submitted = submitAudioSamplingFrequency(*device, stream.endpointAddress, selectedRate);
  if (stream.alternate > 0)
  {
    submitted = submitSetInterface(*device, stream.interfaceNumber, stream.alternate) && submitted;
  }
  if (!submitted)
  {
    return false;
  }

  device->audioOutRunning = true;
  device->audioOutFrameAccumulator = 0;
  device->audioOutUnderruns = 0;

  for (size_t i = 0; i < ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS; i++)
  {
    usb_transfer_t *transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(device->audioOutPacketSize, 1, &transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_alloc(audio OUT request) failed: %s", esp_err_to_name(err));
      setLastError(err);
      releaseAudioOutputTransfers(*device);
      return false;
    }

    transfer->device_handle = device->handle;
    transfer->bEndpointAddress = device->audioOutEndpointAddress;
    transfer->callback = outputTransferCallback;
    transfer->context = this;
    device->audioOutTransfers[i] = transfer;

    if (!submitAudioOutputRequestTransfer(*device, transfer))
    {
      releaseAudioOutputTransfers(*device);
      return false;
    }
  }

  return true;
}

void EspUsbHost::audioOutputStop(uint8_t address)
{
  DeviceState *device = findAudioOutputDevice(address);
  if (!device)
  {
    return;
  }
  device->audioOutRunning = false;
}

bool EspUsbHost::audioOutputRunning(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device && device->audioOutRunning;
}

uint32_t EspUsbHost::audioOutputUnderruns(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device ? device->audioOutUnderruns : 0;
}

bool EspUsbHost::audioSend(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findAudioOutputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioSend() called before a USB Audio OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "audioSend() called with null data");
    return false;
  }
  if (length == 0)
  {
    return true;
  }

  const size_t packetSize = device->audioOutPacketSize;
  if (packetSize == 0)
  {
    ESP_LOGW(TAG, "audioSend() called with invalid audio OUT packet size");
    return false;
  }

  static constexpr int AUDIO_ISOC_MAX_PACKETS = 8;
  const size_t maxTransferSize = packetSize * AUDIO_ISOC_MAX_PACKETS;
  size_t offset = 0;
  while (offset < length)
  {
    const size_t chunkLength = (length - offset) > maxTransferSize ? maxTransferSize : (length - offset);
    if (!submitAudioOutputTransfer(*device, data + offset, chunkLength))
    {
      return false;
    }
    offset += chunkLength;
  }
  return true;
}

size_t EspUsbHost::getAudioFeatureUnits(uint8_t address, EspUsbHostAudioFeatureUnitInfo *units, size_t maxUnits) const
{
  const DeviceState *device = findAudioControlDevice(address);
  if (!device)
  {
    return 0;
  }
  const size_t count = device->audioFeatureUnitCount < maxUnits ? device->audioFeatureUnitCount : maxUnits;
  if (units)
  {
    for (size_t i = 0; i < count; i++)
    {
      units[i] = device->audioFeatureUnits[i];
    }
  }
  return device->audioFeatureUnitCount;
}

bool EspUsbHost::audioHasMute(uint8_t address, uint8_t unitId, uint8_t channel) const
{
  const DeviceState *device = findAudioControlDevice(address);
  return device && findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel);
}

bool EspUsbHost::audioHasVolume(uint8_t address, uint8_t unitId, uint8_t channel) const
{
  const DeviceState *device = findAudioControlDevice(address);
  return device && findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel);
}

bool EspUsbHost::audioGetMute(bool &mute, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  uint8_t value = 0;
  if (!audioFeatureControl(*device, 0x81, unit->unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel, &value, sizeof(value), true, timeoutMs))
  {
    return false;
  }
  mute = value != 0;
  return true;
}

bool EspUsbHost::audioSetMute(bool mute, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  uint8_t value = mute ? 1 : 0;
  return audioFeatureControl(*device, 0x01, unit->unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel, &value, sizeof(value), false, timeoutMs);
}

bool EspUsbHost::audioGetVolume(int16_t &volume, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  uint8_t value[2] = {};
  if (!audioFeatureControl(*device, 0x81, unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), true, timeoutMs))
  {
    return false;
  }
  volume = readLe16s(value);
  return true;
}

bool EspUsbHost::audioSetVolume(int16_t volume, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  uint8_t value[2] = {};
  writeLe16(value, volume);
  return audioFeatureControl(*device, 0x01, unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), false, timeoutMs);
}

bool EspUsbHost::audioGetVolumeRange(EspUsbHostAudioVolumeRange &range, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  uint8_t value[2] = {};
  if (!audioFeatureControl(*device, 0x82, unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), true, timeoutMs))
  {
    return false;
  }
  range.min = readLe16s(value);
  if (!audioFeatureControl(*device, 0x83, unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), true, timeoutMs))
  {
    return false;
  }
  range.max = readLe16s(value);
  if (!audioFeatureControl(*device, 0x84, unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), true, timeoutMs))
  {
    return false;
  }
  range.resolution = readLe16s(value);
  return true;
}

bool EspUsbHost::audioGetVolumeDb(float &db, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  int16_t volume = 0;
  if (!audioGetVolume(volume, address, unitId, channel, timeoutMs))
  {
    return false;
  }
  db = static_cast<float>(volume) / 256.0f;
  return true;
}

bool EspUsbHost::audioSetVolumeDb(float db, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  return audioSetVolume(audioDbToRaw(db), address, unitId, channel, timeoutMs);
}

bool EspUsbHost::audioSetVolumeDbClamped(float db, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  int16_t volume = audioDbToRaw(db);

  EspUsbHostAudioVolumeRange range;
  if (audioGetVolumeRange(range, address, unitId, channel, timeoutMs))
  {
    volume = audioClampVolumeRaw(volume, range);
  }

  return audioSetVolume(volume, address, unitId, channel, timeoutMs);
}

bool EspUsbHost::audioConfigureVolume(float db, bool mute, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioPlaybackFeatureUnit(*device, unitId, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  unitId = unit->unitId;

  bool ok = true;
  if (audioHasMute(address, unitId, channel))
  {
    ok = audioSetMute(mute, address, unitId, channel, timeoutMs) && ok;
  }
  if (audioHasVolume(address, unitId, channel))
  {
    ok = audioSetVolumeDbClamped(db, address, unitId, channel, timeoutMs) && ok;
  }
  return ok;
}

bool EspUsbHost::audioSetVolumePercent(uint8_t percent, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioPlaybackFeatureUnit(*device, unitId, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  unitId = unit->unitId;

  if (percent > 100)
  {
    percent = 100;
  }

  if (percent == 0)
  {
    if (audioHasMute(address, unitId, channel))
    {
      return audioSetMute(true, address, unitId, channel, timeoutMs);
    }
    EspUsbHostAudioVolumeRange range;
    if (!audioGetVolumeRange(range, address, unitId, channel, timeoutMs))
    {
      return false;
    }
    return audioSetVolume(range.min, address, unitId, channel, timeoutMs);
  }

  EspUsbHostAudioVolumeRange range;
  if (!audioGetVolumeRange(range, address, unitId, channel, timeoutMs))
  {
    return false;
  }

  bool ok = true;
  if (audioHasMute(address, unitId, channel))
  {
    ok = audioSetMute(false, address, unitId, channel, timeoutMs);
  }

  const float db = 20.0f * log10f(static_cast<float>(percent) / 100.0f);
  const int16_t volume = audioClampVolumeRaw(audioDbToRaw(db), range);
  return audioSetVolume(volume, address, unitId, channel, timeoutMs) && ok;
}

bool EspUsbHost::audioConfigureVolumePercent(uint8_t percent, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  return audioSetVolumePercent(percent, address, unitId, channel, timeoutMs);
}

bool EspUsbHost::mscCommand(DeviceState &device,
                            const uint8_t *command,
                            uint8_t commandLength,
                            uint8_t *data,
                            size_t dataLength,
                            bool dataIn,
                            uint32_t timeoutMs)
{
  if (!command || commandLength == 0 || commandLength > 16)
  {
    return false;
  }
  if (dataLength > 0 && !data)
  {
    return false;
  }
  if (!device.handle || !device.hasMscInEndpoint || !device.hasMscOutEndpoint)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "MSC block APIs cannot run from USB client task");
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  auto submitAndWait = [&](uint8_t endpointAddress, const uint8_t *out, uint8_t *in, size_t length, size_t &actual) -> bool
  {
    size_t transferLength = length;
    if ((endpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0 && device.mscInPacketSize > 0)
    {
      transferLength = ((length + device.mscInPacketSize - 1) / device.mscInPacketSize) * device.mscInPacketSize;
    }
    const size_t allocLength = transferLength > 0 ? transferLength : 1;
    usb_transfer_t *transfer = nullptr;
    esp_err_t err = usb_host_transfer_alloc(allocLength, 0, &transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_alloc(MSC ep=0x%02x) failed: %s", endpointAddress, esp_err_to_name(err));
      setLastError(err);
      return false;
    }
    if (out && length > 0)
    {
      memcpy(transfer->data_buffer, out, length);
    }
    context.status = USB_TRANSFER_STATUS_ERROR;
    context.actualLength = 0;
    transfer->device_handle = device.handle;
    transfer->bEndpointAddress = endpointAddress;
    transfer->callback = syncTransferCallback;
    transfer->context = &context;
    transfer->num_bytes = transferLength;

    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_submit(MSC ep=0x%02x) failed: %s", endpointAddress, esp_err_to_name(err));
      setLastError(err);
      usb_host_transfer_free(transfer);
      return false;
    }

    const TickType_t waitTicks = pdMS_TO_TICKS(timeoutMs);
    const bool done = xSemaphoreTake(context.done, waitTicks) == pdTRUE;
    if (!done)
    {
      ESP_LOGW(TAG, "MSC transfer timeout ep=0x%02x", endpointAddress);
      usb_host_endpoint_clear(device.handle, endpointAddress);
      usb_host_transfer_free(transfer);
      setLastError(ESP_ERR_TIMEOUT);
      return false;
    }
    actual = context.actualLength;
    const bool ok = context.status == USB_TRANSFER_STATUS_COMPLETED;
    if (ok && in && length > 0)
    {
      memcpy(in, transfer->data_buffer, actual < length ? actual : length);
    }
    usb_host_transfer_free(transfer);
    if (!ok)
    {
      ESP_LOGW(TAG, "MSC transfer failed ep=0x%02x status=%d", endpointAddress, context.status);
      setLastError(ESP_FAIL);
    }
    return ok;
  };

  const uint8_t commandOpcode = command[0];
  const bool allowResetRecovery = commandOpcode != SCSI_CMD_SYNCHRONIZE_CACHE_10;
  bool requestSenseAfterCommand = false;
  const uint32_t tag = device.mscTag++;
  EspUsbHostMscCbw cbw = {};
  cbw.signature = USB_MSC_CBW_SIGNATURE;
  cbw.tag = tag;
  cbw.dataTransferLength = dataLength;
  cbw.flags = dataIn ? 0x80 : 0x00;
  cbw.lun = device.mscLun;
  cbw.commandBlockLength = commandLength;
  memcpy(cbw.commandBlock, command, commandLength);

  size_t actual = 0;
  bool ok = submitAndWait(device.mscOutEndpointAddress,
                          reinterpret_cast<const uint8_t *>(&cbw),
                          nullptr,
                          sizeof(cbw),
                          actual);

  if (ok && dataLength > 0)
  {
    ok = submitAndWait(dataIn ? device.mscInEndpointAddress : device.mscOutEndpointAddress,
                       dataIn ? nullptr : data,
                       dataIn ? data : nullptr,
                       dataLength,
                       actual);
    ok = ok && (!dataIn || actual == dataLength);
  }

  EspUsbHostMscCsw csw = {};
  if (ok)
  {
    actual = 0;
    const bool cswTransferOk = submitAndWait(device.mscInEndpointAddress,
                                             nullptr,
                                             reinterpret_cast<uint8_t *>(&csw),
                                             sizeof(csw),
                                             actual);
    ok = cswTransferOk &&
         actual == sizeof(csw) &&
         csw.signature == USB_MSC_CSW_SIGNATURE &&
         csw.tag == tag;
    if (!cswTransferOk)
    {
      if (allowResetRecovery)
      {
        mscResetRecovery(device, timeoutMs);
      }
    }
    if (cswTransferOk && !ok)
    {
      ESP_LOGW(TAG, "MSC invalid CSW actual=%u signature=0x%08lx tag=0x%08lx expected=0x%08lx",
               static_cast<unsigned>(actual),
               static_cast<unsigned long>(csw.signature),
               static_cast<unsigned long>(csw.tag),
               static_cast<unsigned long>(tag));
      setLastError(ESP_FAIL);
      if (allowResetRecovery)
      {
        mscResetRecovery(device, timeoutMs);
      }
    }
    if (ok && csw.status != USB_MSC_CSW_STATUS_PASSED)
    {
      ESP_LOGW(TAG, "MSC command failed status=%u residue=%lu",
               csw.status,
               static_cast<unsigned long>(csw.dataResidue));
      setLastError(ESP_FAIL);
      ok = false;
      if (csw.status == USB_MSC_CSW_STATUS_PHASE_ERROR)
      {
        if (allowResetRecovery)
        {
          mscResetRecovery(device, timeoutMs);
        }
      }
      else if (commandOpcode != SCSI_CMD_REQUEST_SENSE &&
               commandOpcode != SCSI_CMD_SYNCHRONIZE_CACHE_10)
      {
        requestSenseAfterCommand = true;
      }
    }
  }
  else
  {
    if (allowResetRecovery)
    {
      mscResetRecovery(device, timeoutMs);
    }
  }

  vSemaphoreDelete(context.done);
  if (requestSenseAfterCommand)
  {
    EspUsbHostMscSense sense;
    mscRequestSense(sense, device.info.address, timeoutMs);
  }
  return ok;
}

bool EspUsbHost::mscResetRecovery(DeviceState &device, uint32_t timeoutMs)
{
  if (!device.handle)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "MSC reset recovery cannot run from USB client task");
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(MSC reset) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_MSC_RESET_REQUEST_TYPE;
  setup->bRequest = USB_MSC_RESET_REQUEST;
  setup->wValue = 0;
  setup->wIndex = device.mscInterfaceNumber;
  setup->wLength = 0;

  context.status = USB_TRANSFER_STATUS_ERROR;
  context.actualLength = 0;
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(MSC reset) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  const bool resetOk = done && context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (!done)
  {
    ESP_LOGW(TAG, "MSC reset timeout");
    setLastError(ESP_ERR_TIMEOUT);
  }
  else if (!resetOk)
  {
    ESP_LOGW(TAG, "MSC reset failed status=%d", context.status);
    setLastError(ESP_FAIL);
  }
  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);

  bool clearOk = resetOk;
  if (device.hasMscInEndpoint)
  {
    err = usb_host_endpoint_clear(device.handle, device.mscInEndpointAddress);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_endpoint_clear(MSC IN 0x%02x) failed: %s",
               device.mscInEndpointAddress,
               esp_err_to_name(err));
      setLastError(err);
      clearOk = false;
    }
  }
  if (device.hasMscOutEndpoint)
  {
    err = usb_host_endpoint_clear(device.handle, device.mscOutEndpointAddress);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_endpoint_clear(MSC OUT 0x%02x) failed: %s",
               device.mscOutEndpointAddress,
               esp_err_to_name(err));
      setLastError(err);
      clearOk = false;
    }
  }
  return clearOk;
}

bool EspUsbHost::mscInquiry(EspUsbHostMscInquiry &inquiry, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscInquiry() called before a USB MSC device is ready");
    return false;
  }

  uint8_t command[6] = {};
  uint8_t data[36] = {};
  command[0] = SCSI_CMD_INQUIRY;
  command[4] = sizeof(data);
  if (!mscCommand(*device, command, sizeof(command), data, sizeof(data), true, timeoutMs))
  {
    return false;
  }

  inquiry = EspUsbHostMscInquiry();
  inquiry.peripheralDeviceType = data[0] & 0x1f;
  inquiry.removable = (data[1] & 0x80) != 0;
  memcpy(inquiry.vendor, data + 8, 8);
  memcpy(inquiry.product, data + 16, 16);
  memcpy(inquiry.revision, data + 32, 4);
  inquiry.vendor[8] = '\0';
  inquiry.product[16] = '\0';
  inquiry.revision[4] = '\0';
  return true;
}

bool EspUsbHost::mscRequestSense(EspUsbHostMscSense &sense, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscRequestSense() called before a USB MSC device is ready");
    return false;
  }

  uint8_t command[6] = {};
  uint8_t data[18] = {};
  command[0] = SCSI_CMD_REQUEST_SENSE;
  command[4] = sizeof(data);
  if (!mscCommand(*device, command, sizeof(command), data, sizeof(data), true, timeoutMs))
  {
    return false;
  }

  sense = EspUsbHostMscSense();
  sense.responseCode = data[0] & 0x7f;
  sense.senseKey = data[2] & 0x0f;
  sense.additionalSenseCode = data[12];
  sense.additionalSenseQualifier = data[13];
  device->mscLastSense = sense;
  device->hasMscLastSense = true;
  return true;
}

bool EspUsbHost::mscTestUnitReady(uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscTestUnitReady() called before a USB MSC device is ready");
    return false;
  }

  uint8_t command[6] = {};
  command[0] = SCSI_CMD_TEST_UNIT_READY;
  return mscCommand(*device, command, sizeof(command), nullptr, 0, true, timeoutMs);
}

bool EspUsbHost::mscWaitReady(uint8_t address, uint32_t readyTimeoutMs, uint32_t commandTimeoutMs)
{
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "mscWaitReady() cannot run from USB client task");
    return false;
  }

  const uint32_t started = millis();
  while (true)
  {
    DeviceState *device = findMscDevice(address);
    if (device && mscTestUnitReady(device->info.address, commandTimeoutMs))
    {
      return true;
    }
    if (device)
    {
      EspUsbHostMscSense sense;
      mscRequestSense(sense, device->info.address, commandTimeoutMs);
    }
    if (millis() - started >= readyTimeoutMs)
    {
      return false;
    }
    delay(50);
  }
}

bool EspUsbHost::mscCapacity64(uint64_t &blockCount, uint32_t &blockSize, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscCapacity64() called before a USB MSC device is ready");
    return false;
  }

  if (!mscTestUnitReady(device->info.address, timeoutMs))
  {
    EspUsbHostMscSense sense;
    mscRequestSense(sense, device->info.address, timeoutMs);
    return false;
  }

  uint8_t command[10] = {};
  uint8_t capacity[8] = {};
  command[0] = SCSI_CMD_READ_CAPACITY_10;
  if (!mscCommand(*device, command, 10, capacity, sizeof(capacity), true, timeoutMs))
  {
    return false;
  }

  const uint32_t lastLba = readBe32(capacity);
  blockSize = readBe32(capacity + 4);
  if (lastLba == 0xffffffff)
  {
    uint8_t command16[16] = {};
    uint8_t capacity16[32] = {};
    command16[0] = SCSI_CMD_SERVICE_ACTION_IN_16;
    command16[1] = SCSI_SERVICE_ACTION_READ_CAPACITY_16;
    command16[10] = sizeof(capacity16) >> 24;
    command16[11] = sizeof(capacity16) >> 16;
    command16[12] = sizeof(capacity16) >> 8;
    command16[13] = sizeof(capacity16);
    if (!mscCommand(*device, command16, sizeof(command16), capacity16, sizeof(capacity16), true, timeoutMs))
    {
      return false;
    }
    blockCount = readBe64(capacity16) + 1;
    blockSize = readBe32(capacity16 + 8);
  }
  else
  {
    blockCount = static_cast<uint64_t>(lastLba) + 1;
  }
  device->mscBlockCount64 = blockCount;
  device->mscBlockCount = blockCount > 0xffffffffULL ? 0 : static_cast<uint32_t>(blockCount);
  device->mscBlockSize = blockSize;
  return blockCount > 0 && blockSize > 0;
}

bool EspUsbHost::mscCapacity(uint32_t &blockCount, uint32_t &blockSize, uint8_t address, uint32_t timeoutMs)
{
  uint64_t blockCount64 = 0;
  if (!mscCapacity64(blockCount64, blockSize, address, timeoutMs))
  {
    return false;
  }
  if (blockCount64 > 0xffffffffULL)
  {
    ESP_LOGW(TAG, "mscCapacity() block count exceeds 32-bit range: %llu",
             static_cast<unsigned long long>(blockCount64));
    return false;
  }
  blockCount = static_cast<uint32_t>(blockCount64);
  return true;
}

bool EspUsbHost::mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount, uint8_t address, uint32_t timeoutMs)
{
  return mscReadBlocks64(lba, data, blockCount, address, timeoutMs);
}

bool EspUsbHost::mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount, uint8_t address, uint32_t timeoutMs)
{
  return mscWriteBlocks64(lba, data, blockCount, address, timeoutMs);
}

bool EspUsbHost::mscReadBlocks64(uint64_t lba, uint8_t *data, uint32_t blockCount, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device || !data || blockCount == 0)
  {
    return false;
  }
  if (device->mscBlockSize == 0)
  {
    uint64_t capacityBlocks = 0;
    uint32_t blockSize = 0;
    if (!mscCapacity64(capacityBlocks, blockSize, device->info.address, timeoutMs))
    {
      return false;
    }
  }
  if (device->mscBlockCount64 > 0 &&
      (lba >= device->mscBlockCount64 || blockCount > device->mscBlockCount64 - lba))
  {
    ESP_LOGW(TAG, "mscReadBlocks64() out of range: lba=%llu count=%lu capacity=%llu",
             static_cast<unsigned long long>(lba),
             static_cast<unsigned long>(blockCount),
             static_cast<unsigned long long>(device->mscBlockCount64));
    return false;
  }

  const uint32_t maxBlocksPerTransfer = USB_MSC_MAX_TRANSFER_BYTES / device->mscBlockSize;
  if (maxBlocksPerTransfer == 0)
  {
    return false;
  }

  uint32_t remaining = blockCount;
  uint64_t currentLba = lba;
  uint8_t *currentData = data;
  while (remaining > 0)
  {
    const uint32_t chunkBlocks = remaining > maxBlocksPerTransfer ? maxBlocksPerTransfer : remaining;
    uint8_t command[16] = {};
    uint8_t commandLength = 10;
    if (currentLba <= 0xffffffffULL && chunkBlocks <= 0xffff)
    {
      command[0] = SCSI_CMD_READ_10;
      writeBe32(command + 2, static_cast<uint32_t>(currentLba));
      command[7] = (chunkBlocks >> 8) & 0xff;
      command[8] = chunkBlocks & 0xff;
    }
    else
    {
      command[0] = SCSI_CMD_READ_16;
      writeBe64(command + 2, currentLba);
      writeBe32(command + 10, chunkBlocks);
      commandLength = 16;
    }
    if (!mscCommand(*device, command, commandLength, currentData, static_cast<size_t>(chunkBlocks) * device->mscBlockSize, true, timeoutMs))
    {
      return false;
    }
    remaining -= chunkBlocks;
    currentLba += chunkBlocks;
    currentData += static_cast<size_t>(chunkBlocks) * device->mscBlockSize;
  }
  return true;
}

bool EspUsbHost::mscWriteBlocks64(uint64_t lba, const uint8_t *data, uint32_t blockCount, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device || !data || blockCount == 0)
  {
    return false;
  }
  if (device->mscBlockSize == 0)
  {
    uint64_t capacityBlocks = 0;
    uint32_t blockSize = 0;
    if (!mscCapacity64(capacityBlocks, blockSize, device->info.address, timeoutMs))
    {
      return false;
    }
  }
  if (device->mscBlockCount64 > 0 &&
      (lba >= device->mscBlockCount64 || blockCount > device->mscBlockCount64 - lba))
  {
    ESP_LOGW(TAG, "mscWriteBlocks64() out of range: lba=%llu count=%lu capacity=%llu",
             static_cast<unsigned long long>(lba),
             static_cast<unsigned long>(blockCount),
             static_cast<unsigned long long>(device->mscBlockCount64));
    return false;
  }

  const uint32_t maxBlocksPerTransfer = USB_MSC_MAX_TRANSFER_BYTES / device->mscBlockSize;
  if (maxBlocksPerTransfer == 0)
  {
    return false;
  }

  uint32_t remaining = blockCount;
  uint64_t currentLba = lba;
  const uint8_t *currentData = data;
  while (remaining > 0)
  {
    const uint32_t chunkBlocks = remaining > maxBlocksPerTransfer ? maxBlocksPerTransfer : remaining;
    uint8_t command[16] = {};
    uint8_t commandLength = 10;
    if (currentLba <= 0xffffffffULL && chunkBlocks <= 0xffff)
    {
      command[0] = SCSI_CMD_WRITE_10;
      writeBe32(command + 2, static_cast<uint32_t>(currentLba));
      command[7] = (chunkBlocks >> 8) & 0xff;
      command[8] = chunkBlocks & 0xff;
    }
    else
    {
      command[0] = SCSI_CMD_WRITE_16;
      writeBe64(command + 2, currentLba);
      writeBe32(command + 10, chunkBlocks);
      commandLength = 16;
    }
    if (!mscCommand(*device,
                    command,
                    commandLength,
                    const_cast<uint8_t *>(currentData),
                    static_cast<size_t>(chunkBlocks) * device->mscBlockSize,
                    false,
                    timeoutMs))
    {
      return false;
    }
    remaining -= chunkBlocks;
    currentLba += chunkBlocks;
    currentData += static_cast<size_t>(chunkBlocks) * device->mscBlockSize;
  }
  return true;
}

bool EspUsbHost::mscSynchronizeCache(uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findMscDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "mscSynchronizeCache() called before a USB MSC device is ready");
    return false;
  }

  uint8_t command[10] = {};
  command[0] = SCSI_CMD_SYNCHRONIZE_CACHE_10;
  return mscCommand(*device, command, sizeof(command), nullptr, 0, false, timeoutMs);
}

bool EspUsbHost::mscMount(const char *basePath,
                          uint8_t address,
                          uint8_t lun,
                          uint8_t maxFiles,
                          uint32_t timeoutMs,
                          bool skipSyncCache)
{
  if (!basePath || basePath[0] != '/' || strlen(basePath) >= sizeof(mscFatMounts[0].basePath))
  {
    ESP_LOGW(TAG, "mscMount() invalid basePath");
    return false;
  }
  if (findMscFatMountByPath(basePath))
  {
    ESP_LOGW(TAG, "mscMount() basePath already mounted: %s", basePath);
    return false;
  }
  if (!mscWaitReady(address, timeoutMs, timeoutMs))
  {
    return false;
  }

  DeviceState *device = findMscDevice(address);
  if (!device || !mscSelectLun(lun, device->info.address, timeoutMs))
  {
    return false;
  }

  EspUsbHostMscBlockDeviceInfo blockInfo;
  if (!mscGetBlockDeviceInfo(blockInfo, device->info.address, timeoutMs))
  {
    return false;
  }
  if (blockInfo.blockCount == 0 || blockInfo.blockSize == 0)
  {
    ESP_LOGW(TAG, "mscMount() invalid block device info");
    return false;
  }
  if (blockInfo.blockCount > 0xffffffffULL)
  {
    ESP_LOGW(TAG, "mscMount() block count exceeds FatFs 32-bit LBA limit: %llu",
             static_cast<unsigned long long>(blockInfo.blockCount));
    return false;
  }

  EspUsbHostMscFatMount *mount = nullptr;
  for (EspUsbHostMscFatMount &candidate : mscFatMounts)
  {
    if (!candidate.inUse)
    {
      mount = &candidate;
      break;
    }
  }
  if (!mount)
  {
    ESP_LOGW(TAG, "mscMount() no mount slots available");
    return false;
  }

  BYTE pdrv = FF_DRV_NOT_USED;
  esp_err_t err = ff_diskio_get_drive(&pdrv);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "ff_diskio_get_drive() failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  *mount = EspUsbHostMscFatMount();
  mount->inUse = true;
  mount->host = this;
  mount->address = device->info.address;
  mount->lun = lun;
  mount->pdrv = pdrv;
  mount->blockCount = blockInfo.blockCount;
  mount->blockSize = blockInfo.blockSize;
  mount->skipSyncCache = skipSyncCache;
  strncpy(mount->basePath, basePath, sizeof(mount->basePath) - 1);
  snprintf(mount->fatDrive, sizeof(mount->fatDrive), "%u:", static_cast<unsigned>(pdrv));

  ff_diskio_register(pdrv, &MSC_FAT_DISKIO);

  const esp_vfs_fat_conf_t conf = {
      .base_path = mount->basePath,
      .fat_drive = mount->fatDrive,
      .max_files = maxFiles,
  };
  err = esp_vfs_fat_register_cfg(&conf, &mount->fs);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_vfs_fat_register_cfg(%s) failed: %s", basePath, esp_err_to_name(err));
    ff_diskio_unregister(pdrv);
    *mount = EspUsbHostMscFatMount();
    setLastError(err);
    return false;
  }

  const FRESULT mountResult = f_mount(mount->fs, mount->fatDrive, 1);
  if (mountResult != FR_OK)
  {
    ESP_LOGW(TAG, "f_mount(%s) failed: %d", mount->fatDrive, mountResult);
    esp_vfs_fat_unregister_path(mount->basePath);
    ff_diskio_unregister(pdrv);
    *mount = EspUsbHostMscFatMount();
    setLastError(ESP_FAIL);
    return false;
  }

  return true;
}

bool EspUsbHost::mscUnmount(const char *basePath)
{
  EspUsbHostMscFatMount *mount = findMscFatMountByPath(basePath);
  if (!mount)
  {
    return false;
  }

  if (!mount->skipSyncCache)
  {
    mscSynchronizeCache(mount->address);
  }
  f_mount(nullptr, mount->fatDrive, 0);
  esp_err_t err = esp_vfs_fat_unregister_path(mount->basePath);
  ff_diskio_unregister(mount->pdrv);
  *mount = EspUsbHostMscFatMount();
  if (err != ESP_OK)
  {
    setLastError(err);
    return false;
  }
  return true;
}

bool EspUsbHost::mscMounted(const char *basePath) const
{
  EspUsbHostMscFatMount *mount = findMscFatMountByPath(basePath);
  return mount && mount->host == this;
}

EspUsbHostMscFS::EspUsbHostMscFS() : fs::FS(fs::FSImplPtr(new VFSImpl()))
{
}

EspUsbHostMscFS::~EspUsbHostMscFS()
{
  end();
}

bool EspUsbHostMscFS::begin(EspUsbHost &host,
                            const char *basePath,
                            uint8_t address,
                            uint8_t lun,
                            uint8_t maxFiles,
                            uint32_t timeoutMs,
                            bool skipSyncCache)
{
  if (!basePath || basePath[0] != '/' || strlen(basePath) >= sizeof(basePath_))
  {
    return false;
  }
  if (host_)
  {
    if (strcmp(basePath_, basePath) == 0 && host_->mscMounted(basePath_))
    {
      return true;
    }
    _impl->mountpoint(nullptr);
    host_ = nullptr;
    basePath_[0] = '\0';
  }
  if (!host.mscMount(basePath, address, lun, maxFiles, timeoutMs, skipSyncCache || skipSyncCache_))
  {
    return false;
  }
  strncpy(basePath_, basePath, sizeof(basePath_) - 1);
  host_ = &host;
  _impl->mountpoint(basePath_);
  return true;
}

void EspUsbHostMscFS::end()
{
  if (!host_)
  {
    return;
  }
  _impl->mountpoint(nullptr);
  host_->mscUnmount(basePath_);
  host_ = nullptr;
  basePath_[0] = '\0';
}

bool EspUsbHostMscFS::mounted() const
{
  return host_ && host_->mscMounted(basePath_);
}

const char *EspUsbHostMscFS::basePath() const
{
  return basePath_;
}

void EspUsbHostMscFS::setSkipSyncCache(bool skip)
{
  skipSyncCache_ = skip;
}

bool EspUsbHostMscFS::skipSyncCache() const
{
  return skipSyncCache_;
}

void EspUsbHost::mscUnmountAddress(uint8_t address)
{
  for (EspUsbHostMscFatMount &mount : mscFatMounts)
  {
    if (!mount.inUse || mount.host != this || mount.address != address)
    {
      continue;
    }
    f_mount(nullptr, mount.fatDrive, 0);
    esp_err_t err = esp_vfs_fat_unregister_path(mount.basePath);
    ff_diskio_unregister(mount.pdrv);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "esp_vfs_fat_unregister_path(%s) after MSC disconnect failed: %s",
               mount.basePath,
               esp_err_to_name(err));
      setLastError(err);
    }
    mount = EspUsbHostMscFatMount();
  }
}

bool EspUsbHost::submitAudioOutputTransfer(DeviceState &device, const uint8_t *data, size_t length)
{
  const size_t packetSize = device.audioOutPacketSize;
  const int packetCount = static_cast<int>((length + packetSize - 1) / packetSize);
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetCount * packetSize, packetCount, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(audio OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  memcpy(transfer->data_buffer, data, length);
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = device.audioOutEndpointAddress;
  transfer->callback = outputTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  size_t remaining = length;
  for (int i = 0; i < packetCount; i++)
  {
    const size_t currentPacketSize = remaining > packetSize ? packetSize : remaining;
    transfer->isoc_packet_desc[i].num_bytes = currentPacketSize;
    transfer->isoc_packet_desc[i].actual_num_bytes = 0;
    transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
    remaining -= currentPacketSize;
  }

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(audio OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::isManagedAudioOutputTransfer(const DeviceState &device, const usb_transfer_t *transfer) const
{
  for (size_t i = 0; i < ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS; i++)
  {
    if (device.audioOutTransfers[i] == transfer)
    {
      return true;
    }
  }
  return false;
}

bool EspUsbHost::fillAudioOutputTransfer(DeviceState &device, usb_transfer_t *transfer)
{
  if (!transfer || !transfer->data_buffer)
  {
    return false;
  }

  const size_t bytesPerFrame = static_cast<size_t>(device.audioOutChannels) * device.audioOutBytesPerSample;
  if (bytesPerFrame == 0)
  {
    return false;
  }

  device.audioOutFrameAccumulator += device.audioSampleRate;
  size_t frames = device.audioOutFrameAccumulator / 1000;
  device.audioOutFrameAccumulator %= 1000;
  if (frames == 0)
  {
    frames = 1;
  }

  const size_t maxFrames = device.audioOutPacketSize / bytesPerFrame;
  if (frames > maxFrames)
  {
    frames = maxFrames;
  }
  const size_t byteCount = frames * bytesPerFrame;

  if (device.audioOutBitsPerSample == 8)
  {
    memset(transfer->data_buffer, 0x80, byteCount);
  }
  else
  {
    memset(transfer->data_buffer, 0, byteCount);
  }

  size_t writtenFrames = 0;
  if (audioOutputCallback_)
  {
    EspUsbHostAudioOutputRequest request;
    request.address = device.info.address;
    request.interfaceNumber = device.audioOutInterfaceNumber;
    request.endpointAddress = device.audioOutEndpointAddress;
    request.sampleRate = device.audioSampleRate;
    request.channels = device.audioOutChannels;
    request.bytesPerSample = device.audioOutBytesPerSample;
    request.bitsPerSample = device.audioOutBitsPerSample;
    request.data = transfer->data_buffer;
    request.frameCount = frames;
    request.byteCount = byteCount;
    request.writtenFrames = 0;
    audioOutputCallback_(request);
    writtenFrames = request.writtenFrames > frames ? frames : request.writtenFrames;
  }

  if (writtenFrames < frames)
  {
    device.audioOutUnderruns++;
    const size_t filledBytes = writtenFrames * bytesPerFrame;
    if (filledBytes < byteCount)
    {
      if (device.audioOutBitsPerSample == 8)
      {
        memset(transfer->data_buffer + filledBytes, 0x80, byteCount - filledBytes);
      }
      else
      {
        memset(transfer->data_buffer + filledBytes, 0, byteCount - filledBytes);
      }
    }
  }

  transfer->num_bytes = byteCount;
  transfer->isoc_packet_desc[0].num_bytes = byteCount;
  transfer->isoc_packet_desc[0].actual_num_bytes = 0;
  transfer->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_COMPLETED;
  return byteCount > 0;
}

bool EspUsbHost::submitAudioOutputRequestTransfer(DeviceState &device, usb_transfer_t *transfer)
{
  if (!device.audioOutRunning || !fillAudioOutputTransfer(device, transfer))
  {
    return false;
  }

  esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(audio OUT request) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }
  return true;
}

bool EspUsbHost::midiSend(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findMidiDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "midiSend() called before a MIDI OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "midiSend() called with null data");
    return false;
  }

  const size_t packetSize = length > device->midiOutPacketSize ? length : device->midiOutPacketSize;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(MIDI OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->midiOutEndpointAddress;
  transfer->callback = serialOutTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(MIDI OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_NOTE_ON,
      static_cast<uint8_t>(0x90 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(velocity & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_NOTE_OFF,
      static_cast<uint8_t>(0x80 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(velocity & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_CONTROL_CHANGE,
      static_cast<uint8_t>(0xb0 | (channel & 0x0f)),
      static_cast<uint8_t>(control & 0x7f),
      static_cast<uint8_t>(value & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_PROGRAM_CHANGE,
      static_cast<uint8_t>(0xc0 | (channel & 0x0f)),
      static_cast<uint8_t>(program & 0x7f),
      0};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_POLY_KEYPRESS,
      static_cast<uint8_t>(0xa0 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(pressure & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_CHANNEL_PRESSURE,
      static_cast<uint8_t>(0xd0 | (channel & 0x0f)),
      static_cast<uint8_t>(pressure & 0x7f),
      0};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address)
{
  if (value > 16383)
  {
    value = 16383;
  }
  const uint8_t packet[4] = {
      MIDI_CIN_PITCH_BEND_CHANGE,
      static_cast<uint8_t>(0xe0 | (channel & 0x0f)),
      static_cast<uint8_t>(value & 0x7f),
      static_cast<uint8_t>((value >> 7) & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address)
{
  if (value < -8192)
  {
    value = -8192;
  }
  else if (value > 8191)
  {
    value = 8191;
  }
  return midiSendPitchBend(channel, static_cast<uint16_t>(value + 8192), address);
}

bool EspUsbHost::midiSendSysEx(const uint8_t *data, size_t length, uint8_t address)
{
  if (length == 0)
  {
    return false;
  }
  if (!data)
  {
    ESP_LOGW(TAG, "midiSendSysEx() called with null data");
    return false;
  }
  if (!findMidiDevice(address))
  {
    ESP_LOGW(TAG, "midiSendSysEx() called before a MIDI OUT endpoint is ready");
    return false;
  }

  const size_t packetCount = (length + 2) / 3;
  if (packetCount == 0 || packetCount > 64)
  {
    ESP_LOGW(TAG, "midiSendSysEx() unsupported length=%u", static_cast<unsigned>(length));
    return false;
  }

  uint8_t packets[64 * 4] = {};
  size_t offset = 0;
  size_t out = 0;
  while (offset < length)
  {
    const size_t remaining = length - offset;
    const size_t chunk = remaining >= 3 ? 3 : remaining;
    uint8_t cin = MIDI_CIN_SYSEX_START;
    if (remaining <= 3)
    {
      cin = chunk == 1 ? MIDI_CIN_SYSEX_END_1BYTE : chunk == 2 ? MIDI_CIN_SYSEX_END_2BYTE
                                                               : MIDI_CIN_SYSEX_END_3BYTE;
    }
    packets[out++] = cin;
    packets[out++] = data[offset];
    packets[out++] = chunk > 1 ? data[offset + 1] : 0;
    packets[out++] = chunk > 2 ? data[offset + 2] : 0;
    offset += chunk;
  }

  return midiSend(packets, out, address);
}

int EspUsbHost::lastError() const
{
  return lastError_;
}

const char *EspUsbHost::lastErrorName() const
{
  return esp_err_to_name(lastError_);
}

void EspUsbHost::printDeviceInfo(uint8_t address, bool includeHubInfo, Print &out)
{
  EspUsbHostDeviceInfo device;
  if (!getDevice(address, device))
  {
    out.printf("Device address=%u not found\n", address);
    return;
  }

  out.println();
  out.println("=========== USB Device ===========");
  const uint8_t hubIndex = device.portId >> 4;
  const uint8_t upstreamPort = device.portId & 0x0f;
  out.printf("Address %u portId=0x%02x", device.address, device.portId);
  if (device.parentAddress)
  {
    out.printf(" parent=%u hub_index=%u upstream_port=%u", device.parentAddress, hubIndex, upstreamPort);
  }
  else
  {
    out.printf(" parent=root root_port=%u", device.portId);
    if (device.portId > 1)
    {
      out.print(" note=hub_stack_may_be_flattened");
    }
  }
  out.printf(" speed=%s\n", speedName(device.speed));
  out.printf("VID:PID %04x:%04x class=0x%02x(%s) subclass=0x%02x protocol=0x%02x\n",
             device.vid,
             device.pid,
             device.deviceClass,
             className(device.deviceClass),
             device.deviceSubClass,
             device.deviceProtocol);
  out.printf("Supported=%s hub=%s\n",
             yesNo(device.supported),
             yesNo(device.isHub));
  if (!device.supported)
  {
    out.println("Note: unsupported by this library, but descriptors are available for inspection.");
  }
  out.printf("USB %x.%02x device %x.%02x ep0=%u\n",
             device.usbVersion >> 8,
             device.usbVersion & 0xff,
             device.deviceVersion >> 8,
             device.deviceVersion & 0xff,
             device.maxPacketSize0);
  out.printf("Strings manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
             device.manufacturer,
             device.product,
             device.serial);
  out.printf("Configuration value=%u interfaces=%u total_len=%u attributes=0x%02x(%s remote_wakeup=%s) max_power=%umA\n",
             device.configurationValue,
             device.configurationInterfaceCount,
             device.configurationTotalLength,
             device.configurationAttributes,
             configAttributeName(device.configurationAttributes),
             yesNo(device.configurationAttributes & 0x20),
             device.configurationMaxPower * 2);
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t interfaceCount = getInterfaces(address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
  const size_t endpointCount = getEndpoints(address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
  out.printf("Endpoint channels claimed=%u/%u managed=%u descriptor_endpoints=%u\n",
             static_cast<unsigned>(endpointChannelCount(address)),
             static_cast<unsigned>(maxEndpointChannelCount()),
             static_cast<unsigned>(managedEndpointCount(address)),
             static_cast<unsigned>(endpointCount));
  out.printf("Estimated HCD channels=%u/%u (ep0=%u claimed=%u hub=%u)\n",
             static_cast<unsigned>(estimatedHcdChannelCount(address)),
             static_cast<unsigned>(maxEndpointChannelCount()),
             static_cast<unsigned>(ep0ChannelCount(address)),
             static_cast<unsigned>(endpointChannelCount(address)),
             static_cast<unsigned>(hubEndpointChannelCount(address)));
  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &intf = interfaces[i];
    out.printf("  Interface %u alt=%u class=0x%02x(%s) subclass=0x%02x protocol=0x%02x endpoints=%u claimed=%s claim=%s\n",
               intf.number,
               intf.alternate,
               intf.interfaceClass,
               className(intf.interfaceClass),
               intf.interfaceSubClass,
               intf.interfaceProtocol,
               intf.endpointCount,
               yesNo(intf.claimed),
               claimResultName(intf));
  }

  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    out.printf("    Endpoint iface=%u ep=0x%02x dir=%s type=%s max_packet=%u interval=%u attrs=0x%02x\n",
               ep.interfaceNumber,
               ep.address,
               (ep.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT",
               transferTypeName(ep.attributes),
               ep.maxPacketSize,
               ep.interval,
               ep.attributes);
  }
  EspUsbHostAudioFeatureUnitInfo audioUnits[ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS];
  const size_t audioUnitCount = getAudioFeatureUnits(address, audioUnits, ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS);
  for (size_t i = 0; i < audioUnitCount && i < ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS; i++)
  {
    const EspUsbHostAudioFeatureUnitInfo &unit = audioUnits[i];
    out.printf("  Audio Feature Unit iface=%u unit=%u source=%u channels=%u control_size=%u master=0x%lx\n",
               unit.interfaceNumber,
               unit.unitId,
               unit.sourceId,
               unit.channelCount,
               unit.controlSize,
               static_cast<unsigned long>(unit.masterControls));
    for (uint8_t channel = 0; channel < unit.channelCount; channel++)
    {
      out.printf("    Channel %u controls=0x%lx\n",
                 channel + 1,
                 static_cast<unsigned long>(unit.channelControls[channel]));
    }
  }
  if (includeHubInfo && device.isHub)
  {
    printHubInfo(*this, device.address, true, out);
  }
  out.println("========= USB Device End =========");
  out.println();
}

void EspUsbHost::printAllDeviceInfo(Print &out)
{
  out.println();
  out.println("=========== USB Topology ===========");
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  out.printf("Tracked devices=%u\n", static_cast<unsigned>(count));
  out.printf("Endpoint channels claimed=%u/%u managed=%u\n",
             static_cast<unsigned>(endpointChannelCount()),
             static_cast<unsigned>(maxEndpointChannelCount()),
             static_cast<unsigned>(managedEndpointCount()));
  out.printf("Estimated HCD channels=%u/%u (ep0=%u claimed=%u hub=%u)\n",
             static_cast<unsigned>(estimatedHcdChannelCount()),
             static_cast<unsigned>(maxEndpointChannelCount()),
             static_cast<unsigned>(ep0ChannelCount()),
             static_cast<unsigned>(endpointChannelCount()),
             static_cast<unsigned>(hubEndpointChannelCount()));
  if (count == 0)
  {
    out.println("No USB devices");
    out.println("========= USB Topology End =========");
    return;
  }
  for (size_t i = 0; i < count; i++)
  {
    printDeviceInfo(devices[i].address, false, out);
  }
  for (size_t i = 0; i < count; i++)
  {
    if (devices[i].isHub)
    {
      printHubInfo(*this, devices[i].address, true, out);
    }
  }
  out.println("========= USB Topology End =========");
}

void EspUsbHost::taskEntry(void *arg)
{
  static_cast<EspUsbHost *>(arg)->taskLoop();
}

void EspUsbHost::clientTaskEntry(void *arg)
{
  static_cast<EspUsbHost *>(arg)->clientTaskLoop();
}

void EspUsbHost::taskLoop()
{
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;
  hostConfig.peripheral_map = hostPeripheralMap(config_.port);

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_install() failed: %s", esp_err_to_name(err));
    setLastError(err);
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 10;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = this;

  err = usb_host_client_register(&clientConfig, &clientHandle_);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_client_register() failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_uninstall();
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY)
  {
    created = xTaskCreate(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_);
  }
  else
  {
    created = xTaskCreatePinnedToCore(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_, config_.taskCore);
  }
  if (created != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create USB Host client task");
    setLastError(ESP_ERR_NO_MEM);
    running_ = false;
  }

  ready_ = running_;
  ESP_LOGI(TAG, "USB Host started stack=%lu priority=%u core=%d",
           static_cast<unsigned long>(config_.taskStackSize),
           static_cast<unsigned>(config_.taskPriority),
           static_cast<int>(config_.taskCore));

  while (running_)
  {
    uint32_t eventFlags = 0;
    err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_lib_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }

  if (clientTaskHandle_)
  {
    vTaskDelete(clientTaskHandle_);
    clientTaskHandle_ = nullptr;
  }
  releaseAllEndpoints(true);
  for (DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    releaseInterfaces(device);
    if (device.handle)
    {
      usb_host_device_close(clientHandle_, device.handle);
    }
    device = DeviceState();
  }
  if (clientHandle_)
  {
    usb_host_client_deregister(clientHandle_);
    clientHandle_ = nullptr;
  }
  usb_host_device_free_all();
  usb_host_uninstall();

  ready_ = false;
  taskHandle_ = nullptr;
  ESP_LOGI(TAG, "USB Host stopped");
  vTaskDelete(nullptr);
}

void EspUsbHost::clientTaskLoop()
{
  while (running_)
  {
    esp_err_t err = usb_host_client_handle_events(clientHandle_, pdMS_TO_TICKS(5));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_client_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
    for (DeviceState &device : devices_)
    {
      if (!device.inUse || !device.hasKeyboardInterface || !device.keyboardLedDirty || device.keyboardLedPending)
      {
        continue;
      }
      if (millis() - device.keyboardLedDirtyTimeMs < 20)
      {
        continue;
      }
      uint8_t leds = espUsbHostBuildKeyboardLedReport(device.keyboardNumLock,
                                                      device.keyboardCapsLock,
                                                      device.keyboardScrollLock);
      device.keyboardLedDirty = false;
      sendKeyboardLedReport(device, leds);
    }
    for (EndpointState &ep : endpoints_)
    {
      if (!ep.inUse || !ep.resubmitAfterLed)
      {
        continue;
      }
      DeviceState *dev = findDeviceByHandle(ep.deviceHandle);
      if (dev && (dev->keyboardLedDirty || dev->keyboardLedPending))
      {
        continue;
      }
      ep.resubmitAfterLed = false;
      if (ep.transfer && running_ && ep.deviceHandle)
      {
        esp_err_t resubErr = usb_host_transfer_submit(ep.transfer);
        if (resubErr != ESP_OK && resubErr != ESP_ERR_INVALID_STATE && resubErr != ESP_ERR_NOT_FINISHED)
        {
          ESP_LOGD(TAG, "usb_host_transfer_submit(after LED) failed: %s", esp_err_to_name(resubErr));
        }
      }
    }
    if (millis() - lastHostDeviceScanMs_ >= 500)
    {
      lastHostDeviceScanMs_ = millis();
      scanHostDevices();
    }
  }
  clientTaskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void EspUsbHost::clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg)
{
  static_cast<EspUsbHost *>(arg)->handleClientEvent(eventMsg);
}

void EspUsbHost::handleClientEvent(const usb_host_client_event_msg_t *eventMsg)
{
  switch (eventMsg->event)
  {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    handleNewDevice(eventMsg->new_dev.address);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    handleDeviceGone(eventMsg->dev_gone.dev_hdl);
    break;
  default:
    ESP_LOGD(TAG, "Unhandled client event: %d", eventMsg->event);
    break;
  }
}

void EspUsbHost::handleNewDevice(uint8_t address)
{
  ESP_LOGI(TAG, "Device connected: address=%u", address);
  DeviceState *device = allocateDevice();
  if (!device)
  {
    ESP_LOGW(TAG, "Ignoring device at address=%u because no device slots are available", address);
    setLastError(ESP_ERR_NO_MEM);
    return;
  }
  device->inUse = true;
  device->info.address = address;
  device->serialConfig = defaultSerialConfig_;
  device->audioSampleRate = defaultAudioSampleRate_;

  esp_err_t err = usb_host_device_open(clientHandle_, address, &device->handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_device_open() failed: %s", esp_err_to_name(err));
    setLastError(err);
    *device = DeviceState();
    return;
  }

  usb_device_info_t devInfo = {};
  err = usb_host_device_info(device->handle, &devInfo);
  if (err == ESP_OK)
  {
    device->manufacturer = usbString(devInfo.str_desc_manufacturer);
    device->product = usbString(devInfo.str_desc_product);
    device->serial = usbString(devInfo.str_desc_serial_num);
    device->info.address = devInfo.dev_addr;
    device->info.speed = devInfo.speed;
    device->info.maxPacketSize0 = devInfo.bMaxPacketSize0;
    device->info.configurationValue = devInfo.bConfigurationValue;
    const uint8_t upstreamPort = devInfo.parent.port_num;
    if (devInfo.parent.dev_hdl)
    {
      DeviceState *parent = findDeviceByHandle(devInfo.parent.dev_hdl);
      device->info.parentAddress = parent ? parent->info.address : 0;
      device->info.portId = parent && parent->hubIndex && upstreamPort > 0 && upstreamPort <= 0x0f
                                ? static_cast<uint8_t>((parent->hubIndex << 4) | upstreamPort)
                                : upstreamPort;
      ESP_LOGI(TAG, "Topology address=%u parent_handle=yes parent_address=%u upstream_port=%u portId=0x%02x",
               device->info.address,
               device->info.parentAddress,
               upstreamPort,
               device->info.portId);
    }
    else
    {
      device->info.portId = upstreamPort ? upstreamPort : 0x01;
      ESP_LOGI(TAG, "Topology address=%u parent_handle=no root_port=%u portId=0x%02x",
               device->info.address,
               upstreamPort,
               device->info.portId);
    }
  }

  const usb_device_desc_t *devDesc = nullptr;
  err = usb_host_get_device_descriptor(device->handle, &devDesc);
  if (err == ESP_OK && devDesc)
  {
    device->info.address = address;
    device->info.vid = devDesc->idVendor;
    device->info.pid = devDesc->idProduct;
    device->info.manufacturer = device->manufacturer.c_str();
    device->info.product = device->product.c_str();
    device->info.serial = device->serial.c_str();
    device->info.usbVersion = devDesc->bcdUSB;
    device->info.deviceVersion = devDesc->bcdDevice;
    device->info.deviceClass = devDesc->bDeviceClass;
    device->info.deviceSubClass = devDesc->bDeviceSubClass;
    device->info.deviceProtocol = devDesc->bDeviceProtocol;
    device->info.maxPacketSize0 = devDesc->bMaxPacketSize0;
    ESP_LOGI(TAG, "VID=%04x PID=%04x manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
             device->info.vid, device->info.pid, device->info.manufacturer, device->info.product, device->info.serial);
    device->vendorSerialSupported = isKnownVendorSerial(device->info.vid, device->info.pid);
    if (device->vendorSerialSupported)
    {
      ESP_LOGI(TAG, "%s VCP candidate detected: VID=%04x PID=%04x",
               vendorSerialName(device->info.vid),
               device->info.vid,
               device->info.pid);
    }
  }

  const usb_config_desc_t *configDesc = nullptr;
  err = usb_host_get_active_config_descriptor(device->handle, &configDesc);
  if (err != ESP_OK || !configDesc)
  {
    ESP_LOGE(TAG, "usb_host_get_active_config_descriptor() failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_device_close(clientHandle_, device->handle);
    *device = DeviceState();
    return;
  }
  device->info.configurationValue = configDesc->bConfigurationValue;
  device->info.configurationAttributes = configDesc->bmAttributes;
  device->info.configurationMaxPower = configDesc->bMaxPower;
  device->info.configurationInterfaceCount = configDesc->bNumInterfaces;
  device->info.configurationTotalLength = configDesc->wTotalLength;

  const bool isHub = devDesc && (devDesc->bDeviceClass == USB_CLASS_HUB_VALUE || configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE));
  device->isHub = isHub;
  if (isHub)
  {
    if (nextHubIndex_ <= 0x0f)
    {
      device->hubIndex = nextHubIndex_++;
    }
    else
    {
      ESP_LOGW(TAG, "Hub index exhausted for address=%u", address);
    }
  }
  parseConfigDescriptor(*device, configDesc);
  const bool hasHid = configHasInterfaceClass(configDesc, USB_CLASS_HID_VALUE);
  const bool hasCdc = device->hasCdcControlInterface || device->hasCdcDataInterface;
  const bool hasAudio = device->hasAudioInterface ||
                        device->hasAudioOutEndpoint ||
                        device->audioFeatureUnitCount > 0;
  const bool hasMsc = device->hasMscInterface && device->hasMscInEndpoint && device->hasMscOutEndpoint;
  device->info.supported = isHub || hasHid || hasCdc || hasAudio || hasMsc || device->vendorSerialSupported;
  device->info.isHub = isHub;
  if (deviceConnectedCallback_)
  {
    deviceConnectedCallback_(device->info);
  }
  if (!device->info.supported)
  {
    ESP_LOGI(TAG, "Unsupported device kept for info: address=%u VID=%04x PID=%04x",
             address,
             device->info.vid,
             device->info.pid);
    return;
  }
  if (device->hasKeyboardInterface)
  {
    uint8_t leds = espUsbHostBuildKeyboardLedReport(device->keyboardNumLock,
                                                    device->keyboardCapsLock,
                                                    device->keyboardScrollLock);
    sendKeyboardLedReport(*device, leds);
  }
}

void EspUsbHost::handleDeviceGone(usb_device_handle_t goneHandle)
{
  ESP_LOGI(TAG, "Device disconnected");
  DeviceState *device = findDeviceByHandle(goneHandle);
  if (!device)
  {
    if (goneHandle)
    {
      usb_host_device_close(clientHandle_, goneHandle);
    }
    return;
  }

  EspUsbHostDeviceInfo info = device->info;
  if (device->hasMscInterface)
  {
    mscUnmountAddress(device->info.address);
  }
  releaseEndpoints(*device, false);
  releaseInterfaces(*device);
  if (device->handle)
  {
    usb_host_device_close(clientHandle_, device->handle);
  }

  if (deviceDisconnectedCallback_)
  {
    deviceDisconnectedCallback_(info);
  }
  *device = DeviceState();
}

void EspUsbHost::refreshDeviceTopology(DeviceState &device)
{
  if (!device.inUse || !device.handle)
  {
    return;
  }

  usb_device_info_t devInfo = {};
  if (usb_host_device_info(device.handle, &devInfo) != ESP_OK)
  {
    return;
  }

  const uint8_t upstreamPort = devInfo.parent.port_num;
  device.info.address = devInfo.dev_addr;
  device.info.speed = devInfo.speed;
  device.info.maxPacketSize0 = devInfo.bMaxPacketSize0;
  device.info.configurationValue = devInfo.bConfigurationValue;
  if (devInfo.parent.dev_hdl)
  {
    DeviceState *parent = findDeviceByHandle(devInfo.parent.dev_hdl);
    device.info.parentAddress = parent ? parent->info.address : 0;
    device.info.portId = parent && parent->hubIndex && upstreamPort > 0 && upstreamPort <= 0x0f
                             ? static_cast<uint8_t>((parent->hubIndex << 4) | upstreamPort)
                             : upstreamPort;
  }
  else
  {
    device.info.parentAddress = 0;
    device.info.portId = upstreamPort ? upstreamPort : 0x01;
  }
}

void EspUsbHost::scanHostDevices()
{
  if (!running_ || !clientHandle_)
  {
    return;
  }

  uint8_t addresses[ESP_USB_HOST_MAX_DEVICES * 2] = {};
  int addressCount = 0;
  if (usb_host_device_addr_list_fill(static_cast<int>(sizeof(addresses)), addresses, &addressCount) != ESP_OK)
  {
    return;
  }

  for (int i = 0; i < addressCount; i++)
  {
    const uint8_t address = addresses[i];
    if (address == 0 || findDevice(address))
    {
      continue;
    }

    usb_device_handle_t handle = nullptr;
    if (usb_host_device_open(clientHandle_, address, &handle) != ESP_OK)
    {
      continue;
    }

    const usb_device_desc_t *devDesc = nullptr;
    const usb_config_desc_t *configDesc = nullptr;
    const bool hasDeviceDesc = usb_host_get_device_descriptor(handle, &devDesc) == ESP_OK && devDesc;
    const bool hasConfigDesc = usb_host_get_active_config_descriptor(handle, &configDesc) == ESP_OK && configDesc;
    const bool isHub = hasDeviceDesc && hasConfigDesc &&
                       (devDesc->bDeviceClass == USB_CLASS_HUB_VALUE ||
                        configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE));
    if (!isHub)
    {
      usb_host_device_close(clientHandle_, handle);
      continue;
    }

    DeviceState *device = allocateDevice();
    if (!device)
    {
      usb_host_device_close(clientHandle_, handle);
      return;
    }

    device->inUse = true;
    device->handle = handle;
    device->info.address = address;
    device->serialConfig = defaultSerialConfig_;
    device->audioSampleRate = defaultAudioSampleRate_;

    usb_device_info_t devInfo = {};
    if (usb_host_device_info(device->handle, &devInfo) == ESP_OK)
    {
      device->manufacturer = usbString(devInfo.str_desc_manufacturer);
      device->product = usbString(devInfo.str_desc_product);
      device->serial = usbString(devInfo.str_desc_serial_num);
      device->info.manufacturer = device->manufacturer.c_str();
      device->info.product = device->product.c_str();
      device->info.serial = device->serial.c_str();
    }

    device->info.vid = devDesc->idVendor;
    device->info.pid = devDesc->idProduct;
    device->info.usbVersion = devDesc->bcdUSB;
    device->info.deviceVersion = devDesc->bcdDevice;
    device->info.deviceClass = devDesc->bDeviceClass;
    device->info.deviceSubClass = devDesc->bDeviceSubClass;
    device->info.deviceProtocol = devDesc->bDeviceProtocol;
    device->info.maxPacketSize0 = devDesc->bMaxPacketSize0;
    device->info.configurationValue = configDesc->bConfigurationValue;
    device->info.configurationAttributes = configDesc->bmAttributes;
    device->info.configurationMaxPower = configDesc->bMaxPower;
    device->info.configurationInterfaceCount = configDesc->bNumInterfaces;
    device->info.configurationTotalLength = configDesc->wTotalLength;
    device->isHub = true;
    device->info.isHub = true;
    device->info.supported = true;
    if (nextHubIndex_ <= 0x0f)
    {
      device->hubIndex = nextHubIndex_++;
    }
    else
    {
      ESP_LOGW(TAG, "Hub index exhausted for address=%u", address);
    }

    parseConfigDescriptor(*device, configDesc);
    refreshDeviceTopology(*device);
    ESP_LOGI(TAG, "Tracked hub discovered by address scan: address=%u vid=%04x pid=%04x parent=%u portId=0x%02x",
             device->info.address,
             device->info.vid,
             device->info.pid,
             device->info.parentAddress,
             device->info.portId);

    if (deviceConnectedCallback_)
    {
      deviceConnectedCallback_(device->info);
    }
  }

  for (DeviceState &device : devices_)
  {
    refreshDeviceTopology(device);
  }
}

void EspUsbHost::parseConfigDescriptor(DeviceState &device, const usb_config_desc_t *configDesc)
{
  ESP_LOGI(TAG, "Configuration descriptor: totalLength=%u interfaces=%u value=%u attributes=0x%02x maxPower=%umA",
           configDesc->wTotalLength,
           configDesc->bNumInterfaces,
           configDesc->bConfigurationValue,
           configDesc->bmAttributes,
           configDesc->bMaxPower * 2);

  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;)
  {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength)
    {
      ESP_LOGW(TAG, "Invalid descriptor length=%u offset=%d", length, i);
      return;
    }
    currentDevice_ = &device;
    handleDescriptor(p[i + 1], &p[i]);
    i += length;
  }
  currentDevice_ = nullptr;
}

void EspUsbHost::handleDescriptor(uint8_t descriptorType, const uint8_t *data)
{
  DeviceState *device = currentDevice_;
  if (!device)
  {
    return;
  }

  switch (descriptorType)
  {
  case USB_INTERFACE_DESC:
  {
    const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(data);
    currentInterfaceNumber_ = intf->bInterfaceNumber;
    currentInterfaceAlternate_ = intf->bAlternateSetting;
    currentInterfaceClass_ = intf->bInterfaceClass;
    currentInterfaceSubClass_ = intf->bInterfaceSubClass;
    currentInterfaceProtocol_ = intf->bInterfaceProtocol;
    currentAudioChannels_ = 0;
    currentAudioBytesPerSample_ = 0;
    currentAudioBitsPerSample_ = 0;
    currentAudioSampleRate_ = 0;
    currentAudioSampleRateCount_ = 0;
    memset(currentAudioSampleRates_, 0, sizeof(currentAudioSampleRates_));
    currentAudioSampleRateMin_ = 0;
    currentAudioSampleRateMax_ = 0;
    currentAudioSampleRateResolution_ = 0;
    currentClaimResult_ = ESP_OK;
    if (device->interfaceInfoCount < ESP_USB_HOST_MAX_INTERFACES)
    {
      EspUsbHostInterfaceInfo &info = device->interfaceInfos[device->interfaceInfoCount++];
      info.number = intf->bInterfaceNumber;
      info.alternate = intf->bAlternateSetting;
      info.interfaceClass = intf->bInterfaceClass;
      info.interfaceSubClass = intf->bInterfaceSubClass;
      info.interfaceProtocol = intf->bInterfaceProtocol;
      info.endpointCount = intf->bNumEndpoints;
    }

    ESP_LOGI(TAG, "Interface %u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u",
             currentInterfaceNumber_, currentInterfaceClass_, currentInterfaceSubClass_,
             currentInterfaceProtocol_, intf->bNumEndpoints);

    const bool isVendorSerialInterface = device->vendorSerialSupported &&
                                         currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE &&
                                         !device->hasVendorSerialInterface;
    const bool isMidiInterface = currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
                                 currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_MIDI_STREAMING &&
                                 !device->hasMidiInterface;
    const bool isMscInterface = currentInterfaceClass_ == USB_CLASS_MASS_STORAGE_VALUE &&
                                currentInterfaceSubClass_ == USB_MSC_SUBCLASS_SCSI &&
                                currentInterfaceProtocol_ == USB_MSC_PROTOCOL_BULK_ONLY &&
                                !device->hasMscInterface;
    bool interfaceAlreadyClaimed = false;
    for (uint8_t i = 0; i < device->interfaceCount; i++)
    {
      if (device->interfaces[i] == currentInterfaceNumber_)
      {
        interfaceAlreadyClaimed = true;
        break;
      }
    }
    const bool isAudioControlInterface = currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
                                         currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_CONTROL &&
                                         !interfaceAlreadyClaimed;
    const bool isAudioInterface = currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
                                  currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
                                  intf->bNumEndpoints > 0 &&
                                  !interfaceAlreadyClaimed;
    if (currentInterfaceClass_ == USB_CLASS_HID_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
        isAudioControlInterface ||
        isAudioInterface ||
        isMidiInterface ||
        isMscInterface ||
        isVendorSerialInterface)
    {
      currentClaimResult_ = usb_host_interface_claim(clientHandle_, device->handle, currentInterfaceNumber_, intf->bAlternateSetting);
      for (uint8_t i = 0; i < device->interfaceInfoCount; i++)
      {
        EspUsbHostInterfaceInfo &info = device->interfaceInfos[i];
        if (info.number == currentInterfaceNumber_ && info.alternate == currentInterfaceAlternate_)
        {
          info.claimAttempted = true;
          info.claimResult = currentClaimResult_;
          break;
        }
      }
      if (currentClaimResult_ == ESP_OK && device->interfaceCount < sizeof(device->interfaces))
      {
        device->interfaces[device->interfaceCount++] = currentInterfaceNumber_;
        device->endpointChannelCount = static_cast<uint8_t>(device->endpointChannelCount + intf->bNumEndpoints);
        for (uint8_t i = 0; i < device->interfaceInfoCount; i++)
        {
          EspUsbHostInterfaceInfo &info = device->interfaceInfos[i];
          if (info.number == currentInterfaceNumber_ && info.alternate == currentInterfaceAlternate_)
          {
            info.claimed = true;
            break;
          }
        }
        ESP_LOGI(TAG, "Interface %u claimed endpoints=%u endpoint_channels=%u/%u",
                 currentInterfaceNumber_,
                 intf->bNumEndpoints,
                 static_cast<unsigned>(endpointChannelCount()),
                 static_cast<unsigned>(maxEndpointChannelCount()));
        if (currentInterfaceAlternate_ > 0 && !isAudioInterface)
        {
          submitSetInterface(*device, currentInterfaceNumber_, currentInterfaceAlternate_);
        }
        if (currentInterfaceSubClass_ == HID_SUBCLASS_BOOT_VALUE &&
            currentInterfaceProtocol_ == HID_PROTOCOL_KEYBOARD_VALUE &&
            !device->hasKeyboardInterface)
        {
          device->hasKeyboardInterface = true;
          device->keyboardInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "Keyboard interface ready: iface=%u", device->keyboardInterfaceNumber);
        }
        if (currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE)
        {
          device->hasCdcControlInterface = true;
          device->cdcControlInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC control interface ready: iface=%u", device->cdcControlInterfaceNumber);
          configureCdcAcm(*device);
        }
        else if (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE)
        {
          device->hasCdcDataInterface = true;
          device->cdcDataInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC data interface ready: iface=%u", device->cdcDataInterfaceNumber);
        }
        else if (isVendorSerialInterface)
        {
          device->hasVendorSerialInterface = true;
          device->vendorSerialInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "%s VCP interface ready: iface=%u",
                   vendorSerialName(device->info.vid),
                   device->vendorSerialInterfaceNumber);
          configureVendorSerial(*device);
        }
        else if (isMidiInterface)
        {
          device->hasMidiInterface = true;
          device->midiInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB MIDI interface ready: iface=%u", device->midiInterfaceNumber);
        }
        else if (isMscInterface)
        {
          device->hasMscInterface = true;
          device->mscInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB MSC interface ready: iface=%u", device->mscInterfaceNumber);
        }
        else if (isAudioControlInterface)
        {
          device->audioControlInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB Audio control interface ready: iface=%u", device->audioControlInterfaceNumber);
        }
        else if (isAudioInterface)
        {
          device->hasAudioInterface = true;
          device->audioInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB Audio streaming interface ready: iface=%u alt=%u",
                   device->audioInterfaceNumber,
                   intf->bAlternateSetting);
        }
      }
      else
      {
        ESP_LOGW(TAG, "usb_host_interface_claim(%u) failed: %s endpoints=%u endpoint_channels=%u/%u",
                 currentInterfaceNumber_,
                 esp_err_to_name(currentClaimResult_),
                 intf->bNumEndpoints,
                 static_cast<unsigned>(endpointChannelCount()),
                 static_cast<unsigned>(maxEndpointChannelCount()));
        setLastError(currentClaimResult_);
      }
    }
    break;
  }

  case USB_CS_INTERFACE_DESC:
  {
    if (currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_CONTROL)
    {
      parseAudioControlDescriptor(*device, data);
      break;
    }
    if (currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
        data[0] >= 8 &&
        data[2] == USB_AUDIO_CS_AS_FORMAT_TYPE &&
        data[3] == USB_AUDIO_FORMAT_TYPE_I)
    {
      currentAudioChannels_ = data[4];
      currentAudioBytesPerSample_ = data[5];
      currentAudioBitsPerSample_ = data[6];
      const uint8_t sampleFrequencyType = data[7];
      if (sampleFrequencyType == 0 && data[0] >= 17)
      {
        currentAudioSampleRateMin_ = readAudioSampleRate24(&data[8]);
        currentAudioSampleRateMax_ = readAudioSampleRate24(&data[11]);
        currentAudioSampleRateResolution_ = readAudioSampleRate24(&data[14]);
        currentAudioSampleRate_ = currentAudioSampleRateMin_;
      }
      else if (sampleFrequencyType > 0 && data[0] >= 8 + sampleFrequencyType * 3)
      {
        currentAudioSampleRateCount_ = sampleFrequencyType < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES
                                           ? sampleFrequencyType
                                           : ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES;
        for (uint8_t i = 0; i < currentAudioSampleRateCount_; i++)
        {
          currentAudioSampleRates_[i] = readAudioSampleRate24(&data[8 + i * 3]);
        }
        currentAudioSampleRate_ = currentAudioSampleRates_[0];
      }
      ESP_LOGI(TAG, "USB Audio Type I format: iface=%u channels=%u bytes=%u bits=%u rate=%lu rates=%u",
               currentInterfaceNumber_,
               currentAudioChannels_,
               currentAudioBytesPerSample_,
               currentAudioBitsPerSample_,
               static_cast<unsigned long>(currentAudioSampleRate_),
               currentAudioSampleRateCount_);
    }
    break;
  }

  case USB_ENDPOINT_DESC:
  {
    const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(data);
    if (device->endpointInfoCount < ESP_USB_HOST_MAX_ENDPOINTS)
    {
      EspUsbHostEndpointInfo &info = device->endpointInfos[device->endpointInfoCount++];
      info.address = ep->bEndpointAddress;
      info.interfaceNumber = currentInterfaceNumber_;
      info.attributes = ep->bmAttributes;
      info.maxPacketSize = ep->wMaxPacketSize;
      info.interval = ep->bInterval;
    }
    const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
    const bool isInterrupt = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
    const bool isBulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
    const bool isIsochronous = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_ISOC;

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_MASS_STORAGE_VALUE &&
        currentInterfaceSubClass_ == USB_MSC_SUBCLASS_SCSI &&
        currentInterfaceProtocol_ == USB_MSC_PROTOCOL_BULK_ONLY &&
        isBulk)
    {
      if (isIn)
      {
        device->hasMscInEndpoint = true;
        device->mscInEndpointAddress = ep->bEndpointAddress;
        device->mscInPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "USB MSC bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
      }
      else
      {
        device->hasMscOutEndpoint = true;
        device->mscOutEndpointAddress = ep->bEndpointAddress;
        device->mscOutPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "USB MSC bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
      }
      return;
    }

    const bool isSerialBulkEndpoint = currentClaimResult_ == ESP_OK &&
                                      (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
                                       (device->vendorSerialSupported && currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE)) &&
                                      isBulk;
    if (isSerialBulkEndpoint)
    {
      if (!isIn)
      {
        device->hasSerialOutEndpoint = true;
        device->serialOutEndpointAddress = ep->bEndpointAddress;
        device->serialOutPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "%s bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(device->info.vid),
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
        return;
      }

      EndpointState *endpoint = allocateEndpoint(*device);
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
      if (err != ESP_OK)
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(serial IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->alternate = currentInterfaceAlternate_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = ep->wMaxPacketSize;

      if (submitInputTransfer(*endpoint))
      {
        ESP_LOGI(TAG, "%s bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(device->info.vid),
                 endpoint->interfaceNumber,
                 endpoint->address,
                 ep->wMaxPacketSize);
      }
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_MIDI_STREAMING &&
        isBulk)
    {
      if (!isIn)
      {
        device->hasMidiOutEndpoint = true;
        device->midiOutEndpointAddress = ep->bEndpointAddress;
        device->midiOutPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "USB MIDI bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
        return;
      }

      EndpointState *endpoint = allocateEndpoint(*device);
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
      if (err != ESP_OK)
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(MIDI IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->alternate = currentInterfaceAlternate_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = ep->wMaxPacketSize;

      if (submitInputTransfer(*endpoint))
      {
        ESP_LOGI(TAG, "USB MIDI bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 endpoint->interfaceNumber,
                 endpoint->address,
                 ep->wMaxPacketSize);
      }
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
        isIsochronous)
    {
      static constexpr int AUDIO_ISOC_PACKETS = 8;
      if (!isIn)
      {
        recordAudioStream(*device, ep, false);
        device->hasAudioInterface = true;
        device->audioInterfaceNumber = currentInterfaceNumber_;
        device->hasAudioOutEndpoint = true;
        device->audioOutInterfaceNumber = currentInterfaceNumber_;
        device->audioOutEndpointAddress = ep->bEndpointAddress;
        device->audioOutPacketSize = ep->wMaxPacketSize;
        device->audioOutChannels = currentAudioChannels_;
        device->audioOutBytesPerSample = currentAudioBytesPerSample_;
        device->audioOutBitsPerSample = currentAudioBitsPerSample_;
        device->audioOutInterval = ep->bInterval;
        ESP_LOGI(TAG, "USB Audio isochronous OUT endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize,
                 ep->bInterval);
        return;
      }

      recordAudioStream(*device, ep, true);
      device->hasAudioInterface = true;
      device->audioInterfaceNumber = currentInterfaceNumber_;
      device->hasAudioInEndpoint = true;
      EndpointState *endpoint = allocateEndpoint(*device);
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      const size_t transferSize = static_cast<size_t>(ep->wMaxPacketSize) * AUDIO_ISOC_PACKETS;
      esp_err_t err = usb_host_transfer_alloc(transferSize, AUDIO_ISOC_PACKETS, &endpoint->transfer);
      if (err != ESP_OK)
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(audio IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->alternate = currentInterfaceAlternate_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->audioChannels = currentAudioChannels_;
      endpoint->audioBytesPerSample = currentAudioBytesPerSample_;
      endpoint->audioBitsPerSample = currentAudioBitsPerSample_;
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = transferSize;
      for (int i = 0; i < endpoint->transfer->num_isoc_packets; i++)
      {
        endpoint->transfer->isoc_packet_desc[i].num_bytes = ep->wMaxPacketSize;
        endpoint->transfer->isoc_packet_desc[i].actual_num_bytes = 0;
        endpoint->transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
      }

      ESP_LOGI(TAG, "USB Audio isochronous IN endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               endpoint->interfaceNumber,
               endpoint->address,
               ep->wMaxPacketSize,
               ep->bInterval);
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_HID_VALUE &&
        !isIn &&
        isInterrupt)
    {
      device->hasVendorInterface = true;
      device->hasVendorOutEndpoint = true;
      device->vendorInterfaceNumber = currentInterfaceNumber_;
      device->vendorOutEndpointAddress = ep->bEndpointAddress;
      device->vendorOutPacketSize = ep->wMaxPacketSize;
      ESP_LOGI(TAG, "HID interrupt OUT endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               currentInterfaceNumber_, ep->bEndpointAddress, ep->wMaxPacketSize, ep->bInterval);
      return;
    }

    if (currentClaimResult_ != ESP_OK || currentInterfaceClass_ != USB_CLASS_HID_VALUE || !isIn || !isInterrupt)
    {
      ESP_LOGD(TAG, "Skipping endpoint 0x%02x iface=%u class=0x%02x attrs=0x%02x",
               ep->bEndpointAddress,
               currentInterfaceNumber_,
               currentInterfaceClass_,
               ep->bmAttributes);
      return;
    }

    EndpointState *endpoint = allocateEndpoint(*device);
    if (!endpoint)
    {
      ESP_LOGW(TAG, "No endpoint slots available");
      setLastError(ESP_ERR_NO_MEM);
      return;
    }

    esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
    if (err != ESP_OK)
    {
      endpoint->inUse = false;
      ESP_LOGW(TAG, "usb_host_transfer_alloc() failed: %s", esp_err_to_name(err));
      setLastError(err);
      return;
    }

    endpoint->address = ep->bEndpointAddress;
    endpoint->interfaceNumber = currentInterfaceNumber_;
    endpoint->interfaceClass = currentInterfaceClass_;
    endpoint->interfaceSubClass = currentInterfaceSubClass_;
    endpoint->interfaceProtocol = currentInterfaceProtocol_;
    endpoint->transfer->device_handle = device->handle;
    endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
    endpoint->transfer->callback = transferCallback;
    endpoint->transfer->context = this;
    endpoint->transfer->num_bytes = ep->wMaxPacketSize;

    if (submitInputTransfer(*endpoint))
    {
      ESP_LOGI(TAG, "HID interrupt IN endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               endpoint->interfaceNumber, endpoint->address, ep->wMaxPacketSize, ep->bInterval);
    }
    break;
  }

  case USB_HID_DESC:
    if (data[0] >= 9 && device->hidReportDescriptorCount < ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS)
    {
      const uint8_t descriptorCount = data[5];
      for (uint8_t i = 0; i < descriptorCount; i++)
      {
        const size_t offset = 6 + static_cast<size_t>(i) * 3;
        if (offset + 2 >= data[0])
        {
          break;
        }
        if (data[offset] != USB_HID_REPORT_DESC)
        {
          continue;
        }
        HIDReportDescriptorState &info = device->hidReportDescriptors[device->hidReportDescriptorCount++];
        info.address = device->info.address;
        info.interfaceNumber = currentInterfaceNumber_;
        info.hidVersion = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
        info.countryCode = data[4];
        info.descriptorType = data[offset];
        info.reportedLength = static_cast<uint16_t>(data[offset + 1]) | (static_cast<uint16_t>(data[offset + 2]) << 8);
        ESP_LOGI(TAG, "HID report descriptor available: iface=%u length=%u country=0x%02x",
                 info.interfaceNumber,
                 info.reportedLength,
                 info.countryCode);
        submitHIDReportDescriptorRequest(info);
        break;
      }
    }
    else
    {
      ESP_LOGD(TAG, "HID descriptor");
    }
    break;

  default:
    break;
  }
}

void EspUsbHost::parseAudioControlDescriptor(DeviceState &device, const uint8_t *data)
{
  if (!data || data[0] < 7 || data[2] != USB_AUDIO_AC_FEATURE_UNIT)
  {
    return;
  }

  const uint8_t length = data[0];
  const uint8_t unitId = data[3];
  const uint8_t sourceId = data[4];
  const uint8_t controlSize = data[5];
  if (unitId == 0 || controlSize == 0 || controlSize > 4)
  {
    return;
  }

  const uint8_t controlBytes = length - 7;
  if (controlBytes < controlSize)
  {
    return;
  }
  const uint8_t descriptorChannelCount = (controlBytes / controlSize) - 1;

  EspUsbHostAudioFeatureUnitInfo *unit = nullptr;
  for (EspUsbHostAudioFeatureUnitInfo &candidate : device.audioFeatureUnits)
  {
    if (candidate.unitId == unitId)
    {
      unit = &candidate;
      break;
    }
  }
  if (!unit)
  {
    if (device.audioFeatureUnitCount >= ESP_USB_HOST_MAX_AUDIO_FEATURE_UNITS)
    {
      ESP_LOGD(TAG, "USB Audio Feature Unit ignored, no slots: unit=%u", unitId);
      return;
    }
    unit = &device.audioFeatureUnits[device.audioFeatureUnitCount++];
  }

  *unit = EspUsbHostAudioFeatureUnitInfo();
  unit->address = device.info.address;
  unit->interfaceNumber = currentInterfaceNumber_;
  unit->unitId = unitId;
  unit->sourceId = sourceId;
  unit->controlSize = controlSize;
  unit->channelCount = descriptorChannelCount < ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS
                           ? descriptorChannelCount
                           : ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS;

  auto readControls = [&](uint8_t controlIndex) -> uint32_t
  {
    uint32_t controls = 0;
    const uint8_t *controlData = &data[6 + controlIndex * controlSize];
    for (uint8_t i = 0; i < controlSize; i++)
    {
      controls |= static_cast<uint32_t>(controlData[i]) << (8 * i);
    }
    return controls;
  };

  unit->masterControls = readControls(0);
  for (uint8_t channel = 0; channel < unit->channelCount; channel++)
  {
    unit->channelControls[channel] = readControls(channel + 1);
  }

  ESP_LOGI(TAG, "USB Audio Feature Unit: iface=%u unit=%u source=%u channels=%u master=0x%lx",
           unit->interfaceNumber,
           unit->unitId,
           unit->sourceId,
           unit->channelCount,
           static_cast<unsigned long>(unit->masterControls));
}

void EspUsbHost::recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input)
{
  if (!ep || device.audioStreamInfoCount >= ESP_USB_HOST_MAX_AUDIO_STREAMS)
  {
    return;
  }

  EspUsbHostAudioStreamInfo &info = device.audioStreamInfos[device.audioStreamInfoCount++];
  info.address = device.info.address;
  info.interfaceNumber = currentInterfaceNumber_;
  info.alternate = currentInterfaceAlternate_;
  info.endpointAddress = ep->bEndpointAddress;
  info.input = input;
  info.output = !input;
  info.channels = currentAudioChannels_;
  info.bytesPerSample = currentAudioBytesPerSample_;
  info.bitsPerSample = currentAudioBitsPerSample_;
  info.sampleRate = currentAudioSampleRate_;
  info.sampleRateCount = currentAudioSampleRateCount_;
  for (uint8_t i = 0; i < currentAudioSampleRateCount_; i++)
  {
    info.sampleRates[i] = currentAudioSampleRates_[i];
  }
  info.sampleRateMin = currentAudioSampleRateMin_;
  info.sampleRateMax = currentAudioSampleRateMax_;
  info.sampleRateResolution = currentAudioSampleRateResolution_;
  info.maxPacketSize = ep->wMaxPacketSize;
  info.interval = ep->bInterval;
}

void EspUsbHost::transferCallback(usb_transfer_t *transfer)
{
  static_cast<EspUsbHost *>(transfer->context)->handleTransfer(transfer);
}

bool EspUsbHost::submitInputTransfer(EndpointState &endpoint)
{
  if (!endpoint.transfer || endpoint.transferSubmitted)
  {
    return endpoint.transferSubmitted;
  }

  if (endpoint.transfer->num_isoc_packets > 0)
  {
    endpoint.transfer->num_bytes = 0;
    for (int i = 0; i < endpoint.transfer->num_isoc_packets; i++)
    {
      endpoint.transfer->num_bytes += endpoint.transfer->isoc_packet_desc[i].num_bytes;
      endpoint.transfer->isoc_packet_desc[i].actual_num_bytes = 0;
      endpoint.transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
    }
  }

  esp_err_t err = usb_host_transfer_submit(endpoint.transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(ep=0x%02x) failed: %s",
             endpoint.address,
             esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  endpoint.transferSubmitted = true;
  return true;
}

bool EspUsbHost::submitHIDReportDescriptorRequest(const HIDReportDescriptorState &descriptorState)
{
  DeviceState *device = findDevice(descriptorState.address);
  if (!device || !device->handle || descriptorState.reportedLength == 0)
  {
    return false;
  }

  const size_t requestLength = descriptorState.reportedLength < ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE
                                   ? descriptorState.reportedLength
                                   : ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + requestLength, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(HID report descriptor async) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  HIDReportDescriptorTransferContext *context = new HIDReportDescriptorTransferContext();
  if (!context)
  {
    usb_host_transfer_free(transfer);
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }
  context->host = this;
  context->descriptor.address = descriptorState.address;
  context->descriptor.interfaceNumber = descriptorState.interfaceNumber;
  context->descriptor.hidVersion = descriptorState.hidVersion;
  context->descriptor.countryCode = descriptorState.countryCode;
  context->descriptor.descriptorType = descriptorState.descriptorType;
  context->descriptor.reportedLength = descriptorState.reportedLength;

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = 0x81;
  setup->bRequest = USB_REQUEST_GET_DESCRIPTOR;
  setup->wValue = static_cast<uint16_t>(USB_HID_REPORT_DESC) << 8;
  setup->wIndex = descriptorState.interfaceNumber;
  setup->wLength = requestLength;

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = hidReportDescriptorTransferCallback;
  transfer->context = context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + requestLength;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(HID report descriptor async) failed: %s", esp_err_to_name(err));
    usb_host_transfer_free(transfer);
    delete context;
    setLastError(err);
    return false;
  }
  return true;
}

void EspUsbHost::parseHIDReportDescriptor(DeviceState &device, const EspUsbHostHIDReportDescriptor &descriptor)
{
  size_t writeIndex = device.hidInputFieldCount;
  while (writeIndex > 0 && device.hidInputFields[writeIndex - 1].interfaceNumber == descriptor.interfaceNumber)
  {
    writeIndex--;
  }
  for (size_t i = writeIndex; i < device.hidInputFieldCount; i++)
  {
    device.hidInputFields[i] = {};
  }
  device.hidInputFieldCount = writeIndex;

  struct GlobalState
  {
    uint16_t usagePage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint8_t reportSize = 0;
    uint8_t reportCount = 0;
    uint8_t reportId = 0;
  } global;

  uint16_t usages[32] = {};
  size_t usageCount = 0;
  uint16_t usageMinimum = 0;
  uint16_t usageMaximum = 0;
  bool hasUsageRange = false;
  uint16_t *bitOffsets = new uint16_t[256]();
  if (!bitOffsets)
  {
    return;
  }

  for (size_t i = 0; i < descriptor.length;)
  {
    const uint8_t prefix = descriptor.data[i++];
    if (prefix == 0xfe)
    {
      if (i + 1 >= descriptor.length)
      {
        break;
      }
      const uint8_t itemLength = descriptor.data[i++];
      i++; // long item tag
      i += (i + itemLength <= descriptor.length) ? itemLength : (descriptor.length - i);
      continue;
    }

    const uint8_t sizeCode = prefix & 0x03;
    const size_t itemSize = sizeCode == 3 ? 4 : sizeCode;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag = (prefix >> 4) & 0x0f;
    const size_t available = (i + itemSize <= descriptor.length) ? itemSize : (descriptor.length - i);
    const int32_t signedValue = hidItemValue(&descriptor.data[i], available);
    const uint32_t unsignedValue = hidItemUnsignedValue(&descriptor.data[i], available);

    if (type == 1)
    {
      switch (tag)
      {
      case 0x00:
        global.usagePage = static_cast<uint16_t>(unsignedValue);
        break;
      case 0x01:
        global.logicalMin = signedValue;
        break;
      case 0x02:
        global.logicalMax = signedValue;
        break;
      case 0x07:
        global.reportSize = static_cast<uint8_t>(unsignedValue);
        break;
      case 0x08:
        global.reportId = static_cast<uint8_t>(unsignedValue);
        break;
      case 0x09:
        global.reportCount = static_cast<uint8_t>(unsignedValue);
        break;
      default:
        break;
      }
    }
    else if (type == 2)
    {
      switch (tag)
      {
      case 0x00:
        if (usageCount < sizeof(usages) / sizeof(usages[0]))
        {
          usages[usageCount++] = static_cast<uint16_t>(unsignedValue);
        }
        break;
      case 0x01:
        usageMinimum = static_cast<uint16_t>(unsignedValue);
        hasUsageRange = true;
        break;
      case 0x02:
        usageMaximum = static_cast<uint16_t>(unsignedValue);
        hasUsageRange = true;
        break;
      default:
        break;
      }
    }
    else if (type == 0 && tag == 0x08)
    {
      const uint8_t flags = static_cast<uint8_t>(unsignedValue & 0xff);
      const bool constant = (flags & 0x01) != 0;
      const uint8_t reportCount = global.reportCount == 0 ? 1 : global.reportCount;
      const uint8_t reportSize = global.reportSize;
      uint16_t &bitOffset = bitOffsets[global.reportId];

      for (uint8_t field = 0; field < reportCount; field++)
      {
        if (!constant && reportSize > 0 && device.hidInputFieldCount < ESP_USB_HOST_MAX_HID_INPUT_FIELDS)
        {
          HIDInputFieldState &inputField = device.hidInputFields[device.hidInputFieldCount++];
          inputField.interfaceNumber = descriptor.interfaceNumber;
          inputField.reportId = global.reportId;
          inputField.usagePage = global.usagePage;
          if (field < usageCount)
          {
            inputField.usage = usages[field];
          }
          else if (hasUsageRange && usageMinimum <= usageMaximum)
          {
            const uint16_t usage = static_cast<uint16_t>(usageMinimum + field);
            inputField.usage = usage <= usageMaximum ? usage : usageMaximum;
          }
          inputField.logicalMin = global.logicalMin;
          inputField.logicalMax = global.logicalMax;
          inputField.bitOffset = bitOffset;
          inputField.bitSize = reportSize;
          inputField.flags = flags;
        }
        bitOffset += reportSize;
      }

      usageCount = 0;
      usageMinimum = 0;
      usageMaximum = 0;
      hasUsageRange = false;
    }

    i += available;
    if (available < itemSize)
    {
      break;
    }
  }

  delete[] bitOffsets;
}

size_t EspUsbHost::decodeHIDInputFields(const DeviceState &device,
                                        uint8_t interfaceNumber,
                                        uint8_t reportId,
                                        const uint8_t *data,
                                        size_t length,
                                        EspUsbHostHIDFieldValue *fields,
                                        size_t maxFields) const
{
  if (!data || !fields || maxFields == 0)
  {
    return 0;
  }

  size_t count = 0;
  for (size_t i = 0; i < device.hidInputFieldCount && count < maxFields; i++)
  {
    const HIDInputFieldState &inputField = device.hidInputFields[i];
    if (inputField.interfaceNumber != interfaceNumber || inputField.reportId != reportId)
    {
      continue;
    }
    EspUsbHostHIDFieldValue &value = fields[count++];
    value.reportId = inputField.reportId;
    value.usagePage = inputField.usagePage;
    value.usage = inputField.usage;
    value.logicalMin = inputField.logicalMin;
    value.logicalMax = inputField.logicalMax;
    value.bitOffset = inputField.bitOffset;
    value.bitSize = inputField.bitSize;
    value.flags = inputField.flags;
    const uint32_t rawValue = hidExtractUnsignedBits(data, length, inputField.bitOffset, inputField.bitSize);
    value.value = inputField.logicalMin < 0 ? hidSignExtend(rawValue, inputField.bitSize) : static_cast<int32_t>(rawValue);
  }
  return count;
}

void EspUsbHost::submitPendingTransfers(usb_device_handle_t deviceHandle, uint8_t interfaceNumber)
{
  DeviceState *device = findDeviceByHandle(deviceHandle);
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse ||
        endpoint.deviceHandle != deviceHandle ||
        endpoint.interfaceNumber != interfaceNumber ||
        endpoint.transferSubmitted)
    {
      continue;
    }
    if (endpoint.interfaceClass == USB_CLASS_AUDIO_VALUE &&
        endpoint.interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING)
    {
      if (!device ||
          endpoint.address != device->audioInEndpointAddress ||
          endpoint.alternate != device->audioInAlternate)
      {
        continue;
      }
    }

    submitInputTransfer(endpoint);
  }
}

void EspUsbHost::controlTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  usb_device_handle_t deviceHandle = transfer->device_handle;
  uint8_t setInterfaceNumber = 0;
  bool isSetInterface = false;
  if (transfer->data_buffer && transfer->num_bytes >= USB_SETUP_PACKET_SIZE)
  {
    const usb_setup_packet_t *setup = reinterpret_cast<const usb_setup_packet_t *>(transfer->data_buffer);
    static constexpr uint8_t USB_REQUEST_SET_INTERFACE = 0x0b;
    static constexpr uint8_t USB_STANDARD_REQUEST_TYPE = 0x01;
    isSetInterface = (setup->bRequest == USB_REQUEST_SET_INTERFACE &&
                      setup->bmRequestType == USB_STANDARD_REQUEST_TYPE);
    setInterfaceNumber = setup->wIndex & 0xff;
  }

  bool isLedSetReport = false;
  if (transfer->data_buffer && transfer->num_bytes >= USB_SETUP_PACKET_SIZE)
  {
    const usb_setup_packet_t *setup = reinterpret_cast<const usb_setup_packet_t *>(transfer->data_buffer);
    isLedSetReport = (setup->bmRequestType == HID_SET_REPORT_REQUEST_TYPE &&
                      setup->bRequest == HID_CLASS_REQUEST_SET_REPORT &&
                      (setup->wValue >> 8) == ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT);
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    if (host && isSetInterface)
    {
      host->submitPendingTransfers(deviceHandle, setInterfaceNumber);
    }
  }
  else
  {
    ESP_LOGD(TAG, "control transfer status=%d", transfer->status);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }

  if (host && isLedSetReport)
  {
    DeviceState *device = host->findDeviceByHandle(deviceHandle);
    if (device && device->hasKeyboardInterface)
    {
      device->keyboardLedPending = false;
      if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
      {
        uint8_t currentLeds = espUsbHostBuildKeyboardLedReport(device->keyboardNumLock,
                                                               device->keyboardCapsLock,
                                                               device->keyboardScrollLock);
        if (currentLeds != device->keyboardLedLastSent)
        {
          device->keyboardLedDirty = true;
        }
      }
    }
  }

  usb_host_transfer_free(transfer);
}

void EspUsbHost::hidReportDescriptorTransferCallback(usb_transfer_t *transfer)
{
  HIDReportDescriptorTransferContext *context = static_cast<HIDReportDescriptorTransferContext *>(transfer->context);
  EspUsbHost *host = context ? context->host : nullptr;
  if (host && context && transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    size_t actualLength = transfer->actual_num_bytes;
    if (actualLength >= USB_SETUP_PACKET_SIZE)
    {
      actualLength -= USB_SETUP_PACKET_SIZE;
    }
    else
    {
      actualLength = 0;
    }
    if (actualLength > ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE)
    {
      actualLength = ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTOR_SIZE;
    }

    context->descriptor.length = actualLength;
    if (actualLength > 0)
    {
      memcpy(context->descriptor.data, transfer->data_buffer + USB_SETUP_PACKET_SIZE, actualLength);
    }
    DeviceState *device = host->findDevice(context->descriptor.address);
    if (device)
    {
      host->parseHIDReportDescriptor(*device, context->descriptor);
    }
    if (host->hidReportDescriptorCallback_)
    {
      host->hidReportDescriptorCallback_(context->descriptor);
    }
  }
  else if (host)
  {
    ESP_LOGD(TAG, "HID report descriptor transfer status=%d", transfer->status);
    host->setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  delete context;
}

void EspUsbHost::outputTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  DeviceState *device = host ? host->findDeviceByHandle(transfer->device_handle) : nullptr;
  const bool managedAudioOut = host && device && host->isManagedAudioOutputTransfer(*device, transfer);

  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "output transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }

  if (managedAudioOut)
  {
    if (device->audioOutRunning &&
        transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
        host->running_)
    {
      if (host->submitAudioOutputRequestTransfer(*device, transfer))
      {
        return;
      }
    }

    for (size_t i = 0; i < ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS; i++)
    {
      if (device->audioOutTransfers[i] == transfer)
      {
        device->audioOutTransfers[i] = nullptr;
        break;
      }
    }
    usb_host_transfer_free(transfer);
    return;
  }

  usb_host_transfer_free(transfer);
}

void EspUsbHost::serialOutTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "serial OUT transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }
  usb_host_transfer_free(transfer);
}

void EspUsbHost::handleTransfer(usb_transfer_t *transfer)
{
  EndpointState *endpoint = findEndpoint(transfer->device_handle, transfer->bEndpointAddress);
  if (!endpoint)
  {
    return;
  }
  DeviceState *device = findDeviceByHandle(endpoint->deviceHandle);

  const bool isAudioStreaming = endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
                                endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
      (transfer->actual_num_bytes > 0 || isAudioStreaming))
  {
#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE
    const char *transferKind = "input";
    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE)
    {
      transferKind = "HID input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_CDC_DATA_VALUE)
    {
      transferKind = "CDC serial input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_MIDI_STREAMING)
    {
      transferKind = "USB MIDI input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING)
    {
      transferKind = "USB Audio input";
    }
    else if (device && device->vendorSerialSupported && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE)
    {
      transferKind = "VCP serial input";
    }
    ESP_LOGV(TAG, "%s iface=%u ep=0x%02x len=%u",
             transferKind,
             endpoint->interfaceNumber,
             transfer->bEndpointAddress,
             transfer->actual_num_bytes);
#endif

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE && hidInputCallback_)
    {
      EspUsbHostHIDInput input;
      input.address = endpoint->deviceAddress;
      input.interfaceNumber = endpoint->interfaceNumber;
      if (device)
      {
        input.vid = device->info.vid;
        input.pid = device->info.pid;
        input.manufacturer = device->info.manufacturer;
        input.product = device->info.product;
        input.serial = device->info.serial;
      }
      input.subclass = endpoint->interfaceSubClass;
      input.protocol = endpoint->interfaceProtocol;
      input.data = transfer->data_buffer;
      input.length = transfer->actual_num_bytes;
      hidInputCallback_(input);
    }

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE)
    {
      if (endpoint->interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
          transfer->actual_num_bytes >= 5 &&
          transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE)
      {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
          endpoint->interfaceProtocol == HID_PROTOCOL_KEYBOARD_VALUE)
      {
        handleKeyboard(*endpoint, transfer->data_buffer, transfer->actual_num_bytes, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
               endpoint->interfaceProtocol == HID_PROTOCOL_MOUSE_VALUE)
      {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 9 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_KEYBOARD)
      {
        handleKeyboard(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 3 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL)
      {
        handleConsumerControl(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_SYSTEM_CONTROL)
      {
        handleSystemControl(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_GAMEPAD)
      {
        handleGamepad(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_VENDOR)
      {
        handleHIDVendorInput(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
      }
    }
    else if (endpoint->interfaceClass == USB_CLASS_CDC_DATA_VALUE ||
             (device && device->vendorSerialSupported && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE))
    {
      handleSerial(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE)
    {
      handleUsbVendorData(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_MIDI_STREAMING)
    {
      handleMidi(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (isAudioStreaming)
    {
      handleAudio(*endpoint, transfer);
    }
  }
  else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
  }

  if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      transfer->status == USB_TRANSFER_STATUS_CANCELED)
  {
    return;
  }

  if (running_ && endpoint->deviceHandle)
  {
    if (transfer->num_isoc_packets > 0)
    {
      transfer->num_bytes = 0;
      for (int i = 0; i < transfer->num_isoc_packets; i++)
      {
        transfer->num_bytes += transfer->isoc_packet_desc[i].num_bytes;
        transfer->isoc_packet_desc[i].actual_num_bytes = 0;
        transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
      }
    }
    const bool isKeyboardEndpoint = device &&
                                    device->hasKeyboardInterface &&
                                    endpoint->interfaceNumber == device->keyboardInterfaceNumber;
    if (isKeyboardEndpoint && device->keyboardLedDirty)
    {
      endpoint->resubmitAfterLed = true;
    }
    else
    {
      esp_err_t err = usb_host_transfer_submit(transfer);
      if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FINISHED)
      {
        ESP_LOGD(TAG, "usb_host_transfer_submit() failed: %s", esp_err_to_name(err));
        setLastError(err);
      }
    }
  }
}

void EspUsbHost::handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  if (length < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE)
  {
    return;
  }

  if (!espUsbHostIsBootKeyboardReportValid(data, length))
  {
    ESP_LOGD(TAG, "Ignoring invalid boot keyboard report: %02x %02x %02x %02x %02x %02x %02x %02x",
             data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    return;
  }

  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);

  EspUsbHostKeyboardReport previousReport;
  EspUsbHostKeyboardReport currentReport;
  memcpy(previousReport.data, endpoint.lastKeyboardReport, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
  memcpy(currentReport.data, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);

  // Detect lock key presses (newly pressed = in current but not in previous report)
  if (device)
  {
    const uint8_t *keys = currentReport.data + 2;
    const uint8_t *lastKeys = previousReport.data + 2;
    bool ledChanged = false;
    for (int i = 0; i < 6; i++)
    {
      const uint8_t key = keys[i];
      if (key == 0)
      {
        continue;
      }
      bool existed = false;
      for (int j = 0; j < 6; j++)
      {
        if (lastKeys[j] == key)
        {
          existed = true;
          break;
        }
      }
      if (!existed)
      {
        if (key == HID_KEY_NUM_LOCK)
        {
          device->keyboardNumLock = !device->keyboardNumLock;
          ledChanged = true;
        }
        else if (key == HID_KEY_CAPS_LOCK)
        {
          device->keyboardCapsLock = !device->keyboardCapsLock;
          ledChanged = true;
        }
        else if (key == HID_KEY_SCROLL_LOCK)
        {
          device->keyboardScrollLock = !device->keyboardScrollLock;
          ledChanged = true;
        }
      }
    }
    if (ledChanged)
    {
      device->keyboardLedDirty = true;
      device->keyboardLedDirtyTimeMs = millis();
    }
  }

  const bool capsLock = device ? device->keyboardCapsLock : false;
  const bool numLock = device ? device->keyboardNumLock : false;
  const bool scrollLock = device ? device->keyboardScrollLock : false;

  EspUsbHostKeyboardEvent events[ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS];
  const size_t eventCount = espUsbHostBuildKeyboardEvents(endpoint.interfaceNumber,
                                                          previousReport,
                                                          currentReport,
                                                          keyboardLayout_,
                                                          capsLock,
                                                          numLock,
                                                          events,
                                                          ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);

  if (!endpoint.keyboardReportReady)
  {
    endpoint.keyboardReportReady = true;
  }

  for (size_t i = 0; i < eventCount; i++)
  {
    events[i].address = endpoint.deviceAddress;
    if (device)
    {
      events[i].vid = device->info.vid;
      events[i].pid = device->info.pid;
      events[i].manufacturer = device->info.manufacturer;
      events[i].product = device->info.product;
      events[i].serial = device->info.serial;
    }
    events[i].numLock = numLock;
    events[i].capsLock = capsLock;
    events[i].scrollLock = scrollLock;
    events[i].rawData = rawData;
    events[i].rawLength = rawLength;
    events[i].reportData = data;
    events[i].reportLength = length;
    ESP_LOGD(TAG, "Keyboard %s iface=%u keycode=0x%02x ascii=0x%02x modifiers=0x%02x caps=%d num=%d scroll=%d",
             events[i].pressed ? "press" : "release",
             events[i].interfaceNumber,
             events[i].keycode,
             events[i].ascii,
             events[i].modifiers,
             capsLock,
             numLock,
             scrollLock);
    if (keyboardCallback_)
    {
      keyboardCallback_(events[i]);
    }
  }

  memcpy(endpoint.lastKeyboardReport, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
}

void EspUsbHost::handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!mouseCallback_)
  {
    return;
  }

  const uint8_t *rawData = data;
  const size_t rawLength = length;

  if (endpoint.interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
      length >= 5 &&
      data[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE)
  {
    data += 1;
    length -= 1;
  }

  EspUsbHostMouseEvent event;
  if (!espUsbHostParseBootMouseReport(endpoint.interfaceNumber,
                                      data,
                                      length,
                                      endpoint.lastMouseButtons,
                                      event))
  {
    return;
  }
  event.address = endpoint.deviceAddress;
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device)
  {
    event.vid = device->info.vid;
    event.pid = device->info.pid;
    event.manufacturer = device->info.manufacturer;
    event.product = device->info.product;
    event.serial = device->info.serial;
  }
  event.rawData = rawData;
  event.rawLength = rawLength;
  event.reportData = data;
  event.reportLength = length;

  ESP_LOGD(TAG, "Mouse iface=%u x=%d y=%d wheel=%d buttons=0x%02x previous=0x%02x",
           event.interfaceNumber,
           event.x,
           event.y,
           event.wheel,
           event.buttons,
           event.previousButtons);
  mouseCallback_(event);
  endpoint.lastMouseButtons = event.buttons;
}

void EspUsbHost::handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!data || length == 0)
  {
    return;
  }

  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device && device->vendorSerialSupported && device->info.vid == 0x0403)
  {
    if (length <= 2)
    {
      return;
    }
    data += 2;
    length -= 2;
  }

  for (EspUsbHostCdcSerial *serialPort : cdcSerials_)
  {
    if (serialPort && serialPort->accepts(endpoint.deviceAddress))
    {
      serialPort->pushData(data, length);
    }
  }

  if (!serialDataCallback_)
  {
    return;
  }

  EspUsbHostSerialData serial;
  serial.address = endpoint.deviceAddress;
  serial.interfaceNumber = endpoint.interfaceNumber;
  serial.data = data;
  serial.length = length;
  serialDataCallback_(serial);
}

void EspUsbHost::handleMidi(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!midiMessageCallback_ || !data || length < 4)
  {
    return;
  }

  for (size_t offset = 0; offset + 3 < length; offset += 4)
  {
    const uint8_t *packet = data + offset;
    EspUsbHostMidiMessage message;
    message.address = endpoint.deviceAddress;
    message.interfaceNumber = endpoint.interfaceNumber;
    message.cable = packet[0] >> 4;
    message.codeIndex = packet[0] & 0x0f;
    message.status = packet[1];
    message.data1 = packet[2];
    message.data2 = packet[3];
    message.raw = packet;
    message.length = 4;

    ESP_LOGD(TAG, "MIDI iface=%u cable=%u cin=0x%02x status=0x%02x data1=0x%02x data2=0x%02x",
             message.interfaceNumber,
             message.cable,
             message.codeIndex,
             message.status,
             message.data1,
             message.data2);
    midiMessageCallback_(message);
  }
}

void EspUsbHost::handleAudio(EndpointState &endpoint, usb_transfer_t *transfer)
{
  if (!audioDataCallback_ || !transfer || !transfer->data_buffer || transfer->num_isoc_packets <= 0)
  {
    return;
  }

  size_t offset = 0;
  for (int i = 0; i < transfer->num_isoc_packets; i++)
  {
    const usb_isoc_packet_desc_t &packet = transfer->isoc_packet_desc[i];
    if (packet.status == USB_TRANSFER_STATUS_COMPLETED && packet.actual_num_bytes > 0)
    {
      EspUsbHostAudioData audio;
      audio.address = endpoint.deviceAddress;
      audio.interfaceNumber = endpoint.interfaceNumber;
      audio.data = transfer->data_buffer + offset;
      audio.length = packet.actual_num_bytes;
      audioDataCallback_(audio);
    }
    offset += packet.num_bytes;
  }
}

void EspUsbHost::handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  if (!consumerControlCallback_)
  {
    return;
  }

  EspUsbHostConsumerControlEvent event;
  if (!espUsbHostParseConsumerControlReport(endpoint.interfaceNumber,
                                            data,
                                            length,
                                            endpoint.lastConsumerUsage,
                                            event))
  {
    return;
  }
  event.address = endpoint.deviceAddress;
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device)
  {
    event.vid = device->info.vid;
    event.pid = device->info.pid;
    event.manufacturer = device->info.manufacturer;
    event.product = device->info.product;
    event.serial = device->info.serial;
  }
  event.rawData = rawData;
  event.rawLength = rawLength;
  event.reportData = data;
  event.reportLength = length;

  ESP_LOGD(TAG, "ConsumerControl iface=%u usage=0x%04x pressed=%u released=%u",
           event.interfaceNumber,
           event.usage,
           event.pressed ? 1 : 0,
           event.released ? 1 : 0);
  consumerControlCallback_(event);
  endpoint.lastConsumerUsage = event.pressed ? event.usage : 0;
}

void EspUsbHost::handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  if (!gamepadCallback_)
  {
    return;
  }

  EspUsbHostGamepadEvent event;
  if (!espUsbHostParseGamepadReport(endpoint.interfaceNumber,
                                    data,
                                    length,
                                    endpoint.lastGamepadState,
                                    event))
  {
    return;
  }
  event.address = endpoint.deviceAddress;
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device)
  {
    event.vid = device->info.vid;
    event.pid = device->info.pid;
    event.manufacturer = device->info.manufacturer;
    event.product = device->info.product;
    event.serial = device->info.serial;
  }
  event.rawData = rawData;
  event.rawLength = rawLength;
  event.reportData = data;
  event.reportLength = length;
  const uint8_t reportId = rawLength > 0 && rawData && rawData != data ? rawData[0] : 0;
  if (device)
  {
    endpoint.hidFieldValueCount = decodeHIDInputFields(*device,
                                                       endpoint.interfaceNumber,
                                                       reportId,
                                                       data,
                                                       length,
                                                       endpoint.hidFieldValues,
                                                       ESP_USB_HOST_MAX_HID_EVENT_FIELDS);
    event.fields = endpoint.hidFieldValues;
    event.fieldCount = endpoint.hidFieldValueCount;
  }

  ESP_LOGD(TAG, "Gamepad iface=%u report_len=%u fields=%u",
           event.interfaceNumber,
           static_cast<unsigned>(event.reportLength),
           static_cast<unsigned>(event.fieldCount));
  gamepadCallback_(event);
  endpoint.lastGamepadState = {};
  endpoint.lastGamepadState.reportLength = event.reportLength < ESP_USB_HOST_GAMEPAD_MAX_REPORT_BYTES
                                               ? event.reportLength
                                               : ESP_USB_HOST_GAMEPAD_MAX_REPORT_BYTES;
  memcpy(endpoint.lastGamepadState.reportData, event.reportData, endpoint.lastGamepadState.reportLength);
}

void EspUsbHost::handleHIDVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device && !device->hasVendorInterface)
  {
    device->hasVendorInterface = true;
    device->vendorInterfaceNumber = endpoint.interfaceNumber;
    ESP_LOGI(TAG, "Vendor HID interface ready: address=%u iface=%u", device->info.address, device->vendorInterfaceNumber);
  }

  if (!hidVendorInputCallback_)
  {
    return;
  }

  EspUsbHostHIDVendorInput input;
  input.address = endpoint.deviceAddress;
  input.interfaceNumber = endpoint.interfaceNumber;
  if (device)
  {
    input.vid = device->info.vid;
    input.pid = device->info.pid;
    input.manufacturer = device->info.manufacturer;
    input.product = device->info.product;
    input.serial = device->info.serial;
  }
  input.rawData = rawData;
  input.rawLength = rawLength;
  input.reportData = data;
  input.reportLength = length;

  ESP_LOGD(TAG, "VendorInput iface=%u length=%u",
           input.interfaceNumber,
           static_cast<unsigned>(input.reportLength));
  hidVendorInputCallback_(input);
}

void EspUsbHost::handleUsbVendorData(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (!device || !device->hasUsbVendorInterface ||
      endpoint.interfaceNumber != device->usbVendorInterfaceNumber)
  {
    return;
  }

  for (size_t i = 0; i < length; i++)
  {
    if (device->usbVendorRxCount == ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE)
    {
      device->usbVendorRxTail = (device->usbVendorRxTail + 1) % ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE;
      device->usbVendorRxCount--;
    }
    device->usbVendorRxBuffer[device->usbVendorRxHead] = data[i];
    device->usbVendorRxHead = (device->usbVendorRxHead + 1) % ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE;
    device->usbVendorRxCount++;
  }

  if (vendorDataCallback_)
  {
    EspUsbHostVendorData event;
    event.address = endpoint.deviceAddress;
    event.interfaceNumber = endpoint.interfaceNumber;
    event.endpoint = endpoint.address;
    event.data = data;
    event.length = length;
    vendorDataCallback_(event);
  }
}

void EspUsbHost::handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  if (!systemControlCallback_)
  {
    return;
  }

  EspUsbHostSystemControlEvent event;
  if (!espUsbHostParseSystemControlReport(endpoint.interfaceNumber,
                                          data,
                                          length,
                                          endpoint.lastSystemUsage,
                                          event))
  {
    return;
  }
  event.address = endpoint.deviceAddress;
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device)
  {
    event.vid = device->info.vid;
    event.pid = device->info.pid;
    event.manufacturer = device->info.manufacturer;
    event.product = device->info.product;
    event.serial = device->info.serial;
  }
  event.rawData = rawData;
  event.rawLength = rawLength;
  event.reportData = data;
  event.reportLength = length;

  ESP_LOGD(TAG, "SystemControl iface=%u usage=0x%02x pressed=%u released=%u",
           event.interfaceNumber,
           event.usage,
           event.pressed ? 1 : 0,
           event.released ? 1 : 0);
  systemControlCallback_(event);
  endpoint.lastSystemUsage = event.pressed ? event.usage : 0;
}

EspUsbHost::EndpointState *EspUsbHost::findEndpoint(usb_device_handle_t deviceHandle, uint8_t endpointAddress)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (endpoint.inUse && endpoint.deviceHandle == deviceHandle && endpoint.address == endpointAddress)
    {
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::EndpointState *EspUsbHost::allocateEndpoint(DeviceState &device)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse)
    {
      endpoint = EndpointState();
      endpoint.inUse = true;
      endpoint.deviceIndex = static_cast<uint8_t>(&device - devices_);
      endpoint.deviceAddress = device.info.address;
      endpoint.deviceHandle = device.handle;
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::allocateDevice()
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      device = DeviceState();
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findDeviceByHandle(usb_device_handle_t handle)
{
  for (DeviceState &device : devices_)
  {
    if (device.inUse && device.handle == handle)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findSerialDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findSerialDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findSerialDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasSerialOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findMidiDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findMidiDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findMidiDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasMidiOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findAudioOutputDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findAudioOutputDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioOutputDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasAudioOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findAudioInputDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findAudioInputDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioInputDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasAudioInEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.hasAudioInterface)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findAudioControlDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findAudioControlDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioControlDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || device.audioControlInterfaceNumber == 0xff || device.audioFeatureUnitCount == 0)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

const EspUsbHostAudioFeatureUnitInfo *EspUsbHost::findAudioFeatureUnit(const DeviceState &device,
                                                                       uint8_t unitId,
                                                                       uint8_t controlSelector,
                                                                       uint8_t channel) const
{
  if (channel > ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS)
  {
    return nullptr;
  }

  for (uint8_t i = 0; i < device.audioFeatureUnitCount; i++)
  {
    const EspUsbHostAudioFeatureUnitInfo &unit = device.audioFeatureUnits[i];
    if (unitId != 0 && unit.unitId != unitId)
    {
      continue;
    }
    if (channel > unit.channelCount)
    {
      continue;
    }
    const uint32_t controls = channel == 0 ? unit.masterControls : unit.channelControls[channel - 1];
    if ((controls & (1UL << (controlSelector - 1))) == 0)
    {
      continue;
    }
    return &unit;
  }
  return nullptr;
}

const EspUsbHostAudioFeatureUnitInfo *EspUsbHost::findAudioPlaybackFeatureUnit(const DeviceState &device,
                                                                               uint8_t unitId,
                                                                               uint8_t channel) const
{
  if (channel > ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS)
  {
    return nullptr;
  }

  if (unitId != 0)
  {
    for (uint8_t i = 0; i < device.audioFeatureUnitCount; i++)
    {
      const EspUsbHostAudioFeatureUnitInfo &unit = device.audioFeatureUnits[i];
      if (unit.unitId == unitId && channel <= unit.channelCount)
      {
        return &unit;
      }
    }
    return nullptr;
  }

  const EspUsbHostAudioFeatureUnitInfo *muteOnlyUnit = nullptr;
  const EspUsbHostAudioFeatureUnitInfo *volumeOnlyUnit = nullptr;
  for (uint8_t i = 0; i < device.audioFeatureUnitCount; i++)
  {
    const EspUsbHostAudioFeatureUnitInfo &unit = device.audioFeatureUnits[i];
    if (channel > unit.channelCount)
    {
      continue;
    }
    const uint32_t controls = channel == 0 ? unit.masterControls : unit.channelControls[channel - 1];
    const bool hasMute = (controls & (1UL << (USB_AUDIO_FEATURE_MUTE_CONTROL - 1))) != 0;
    const bool hasVolume = (controls & (1UL << (USB_AUDIO_FEATURE_VOLUME_CONTROL - 1))) != 0;
    if (hasMute && hasVolume)
    {
      return &unit;
    }
    if (hasMute && !muteOnlyUnit)
    {
      muteOnlyUnit = &unit;
    }
    if (hasVolume && !volumeOnlyUnit)
    {
      volumeOnlyUnit = &unit;
    }
  }

  return muteOnlyUnit ? muteOnlyUnit : volumeOnlyUnit;
}

EspUsbHost::DeviceState *EspUsbHost::findMscDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findMscDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findMscDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse ||
        !device.handle ||
        !device.hasMscInterface ||
        !device.hasMscInEndpoint ||
        !device.hasMscOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findKeyboardDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findKeyboardDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findKeyboardDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasKeyboardInterface)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findHIDVendorDevice(uint8_t address)
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || (!device.hasVendorInterface && !device.hasVendorOutEndpoint))
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findUsbVendorDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findUsbVendorDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findUsbVendorDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasUsbVendorInterface)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findUsbVendorCandidate(uint8_t address, uint8_t interfaceNumber)
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle)
    {
      continue;
    }
    if (address != ESP_USB_HOST_ANY_ADDRESS && device.info.address != address)
    {
      continue;
    }

    for (uint8_t i = 0; i < device.interfaceInfoCount; i++)
    {
      const EspUsbHostInterfaceInfo &intf = device.interfaceInfos[i];
      if (intf.interfaceClass != USB_CLASS_VENDOR_VALUE)
      {
        continue;
      }
      if (interfaceNumber != 0xff && intf.number != interfaceNumber)
      {
        continue;
      }
      return &device;
    }
  }
  return nullptr;
}

size_t EspUsbHost::deviceCount() const
{
  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (device.inUse)
    {
      count++;
    }
  }
  return count;
}

size_t EspUsbHost::getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const
{
  if (!devices || maxDevices == 0)
  {
    return 0;
  }

  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    devices[count++] = device.info;
    if (count >= maxDevices)
    {
      break;
    }
  }
  return count;
}

bool EspUsbHost::getDevice(uint8_t address, EspUsbHostDeviceInfo &deviceInfo) const
{
  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return false;
  }
  deviceInfo = device->info;
  return true;
}

size_t EspUsbHost::getHostDeviceAddresses(uint8_t *addresses, size_t maxAddresses) const
{
  if (!addresses || maxAddresses == 0)
  {
    return 0;
  }
  int count = 0;
  esp_err_t err = usb_host_device_addr_list_fill(static_cast<int>(maxAddresses), addresses, &count);
  if (err != ESP_OK)
  {
    return 0;
  }
  return count > 0 ? static_cast<size_t>(count) : 0;
}

bool EspUsbHost::probeHostDevice(uint8_t address, EspUsbHostDeviceProbeInfo &probe)
{
  probe = EspUsbHostDeviceProbeInfo();
  probe.address = address;
  if (!running_ || !clientHandle_ || address == 0)
  {
    return false;
  }

  usb_device_handle_t handle = nullptr;
  DeviceState *knownDevice = findDevice(address);
  const bool openedTemporarily = !(knownDevice && knownDevice->handle);
  if (openedTemporarily)
  {
    esp_err_t err = usb_host_device_open(clientHandle_, address, &handle);
    if (err != ESP_OK)
    {
      return false;
    }
  }
  else
  {
    handle = knownDevice->handle;
  }
  probe.openOk = true;

  usb_device_info_t devInfo = {};
  if (usb_host_device_info(handle, &devInfo) == ESP_OK)
  {
    probe.deviceInfoOk = true;
    probe.address = devInfo.dev_addr;
    probe.parentPort = devInfo.parent.port_num;
    probe.speed = static_cast<uint8_t>(devInfo.speed);
    if (devInfo.parent.dev_hdl)
    {
      DeviceState *parent = findDeviceByHandle(devInfo.parent.dev_hdl);
      probe.parentAddress = parent ? parent->info.address : 0;
    }
  }

  const usb_device_desc_t *devDesc = nullptr;
  if (usb_host_get_device_descriptor(handle, &devDesc) == ESP_OK && devDesc)
  {
    probe.deviceDescriptorOk = true;
    probe.vid = devDesc->idVendor;
    probe.pid = devDesc->idProduct;
    probe.deviceClass = devDesc->bDeviceClass;
    probe.deviceSubClass = devDesc->bDeviceSubClass;
    probe.deviceProtocol = devDesc->bDeviceProtocol;
  }

  const usb_config_desc_t *configDesc = nullptr;
  if (usb_host_get_active_config_descriptor(handle, &configDesc) == ESP_OK && configDesc)
  {
    probe.configDescriptorOk = true;
    probe.interfaceCount = configDesc->bNumInterfaces;
    probe.configHasHubInterface = configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE);
  }

  if (openedTemporarily)
  {
    usb_host_device_close(clientHandle_, handle);
  }

  probe.hubDescriptorOk = getHubInfo(address, probe.hub);
  return probe.openOk;
}

bool EspUsbHost::getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub)
{
  hub = EspUsbHostHubInfo();
  if (!running_ || !clientHandle_)
  {
    ESP_LOGW(TAG, "getHubInfo() called before USB Host is ready");
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "getHubInfo() cannot run from USB client task");
    return false;
  }
  if (hubAddress == 0)
  {
    ESP_LOGW(TAG, "getHubInfo() invalid hubAddress=0");
    return false;
  }

  usb_device_handle_t hubHandle = nullptr;
  DeviceState *knownHub = findDevice(hubAddress);
  const bool openedTemporarily = !(knownHub && knownHub->handle);
  if (openedTemporarily)
  {
    esp_err_t err = usb_host_device_open(clientHandle_, hubAddress, &hubHandle);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_device_open(hub=%u) failed: %s", hubAddress, esp_err_to_name(err));
      setLastError(err);
      return false;
    }
  }
  else
  {
    hubHandle = knownHub->handle;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + sizeof(hub.rawDescriptor), 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(hub descriptor) failed: %s", esp_err_to_name(err));
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  HubControlTransferContext *context = new HubControlTransferContext();
  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_HUB_DESCRIPTOR_REQUEST_TYPE;
  setup->bRequest = USB_REQUEST_GET_DESCRIPTOR;
  setup->wValue = static_cast<uint16_t>(USB_DESCRIPTOR_TYPE_HUB) << 8;
  setup->wIndex = 0;
  setup->wLength = sizeof(hub.rawDescriptor);

  transfer->device_handle = hubHandle;
  transfer->bEndpointAddress = 0;
  transfer->callback = hubControlTransferCallback;
  transfer->context = context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + sizeof(hub.rawDescriptor);

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(HUB descriptor) failed: %s", esp_err_to_name(err));
    usb_host_transfer_free(transfer);
    delete context;
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    setLastError(err);
    return false;
  }

  const uint32_t deadline = millis() + 1000;
  while (!context->done && millis() < deadline)
  {
    delay(1);
  }

  const bool done = context->done;
  const usb_transfer_status_t transferStatus = context->status;
  const bool ok = done && transferStatus == USB_TRANSFER_STATUS_COMPLETED;
  if (ok)
  {
    const uint8_t *data = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
    hub.address = hubAddress;
    hub.descriptorLength = data[0] <= sizeof(hub.rawDescriptor) ? data[0] : sizeof(hub.rawDescriptor);
    memcpy(hub.rawDescriptor, data, hub.descriptorLength);
    if (hub.descriptorLength >= 7)
    {
      hub.portCount = data[2];
      hub.characteristics = static_cast<uint16_t>(data[3]) | (static_cast<uint16_t>(data[4]) << 8);
      const uint8_t powerMode = hub.characteristics & 0x0003;
      hub.gangedPowerSwitching = powerMode == 0;
      hub.perPortPowerSwitching = powerMode == 1;
      hub.noPowerSwitching = powerMode == 2 || powerMode == 3;
      hub.compound = (hub.characteristics & 0x0004) != 0;
      const uint8_t overCurrentMode = (hub.characteristics >> 3) & 0x0003;
      hub.gangedOverCurrent = overCurrentMode == 0;
      hub.perPortOverCurrent = overCurrentMode == 1;
      hub.noOverCurrent = overCurrentMode == 2 || overCurrentMode == 3;
      hub.powerOnToPowerGoodMs = static_cast<uint16_t>(data[5]) * 2;
      hub.controllerCurrentMa = data[6];
    }
  }
  else if (!done)
  {
    ESP_LOGW(TAG, "HUB descriptor request timed out hub=%u", hubAddress);
    setLastError(ESP_ERR_TIMEOUT);
    if (openedTemporarily)
    {
      usb_host_device_close(clientHandle_, hubHandle);
    }
    return false;
  }
  else
  {
    ESP_LOGW(TAG, "HUB descriptor request failed status=%d hub=%u", transferStatus, hubAddress);
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  delete context;
  if (openedTemporarily)
  {
    usb_host_device_close(clientHandle_, hubHandle);
  }
  return ok && hub.descriptorLength >= 7;
}

size_t EspUsbHost::getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const
{
  if (!interfaces || maxInterfaces == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->interfaceInfoCount < maxInterfaces ? device->interfaceInfoCount : maxInterfaces;
  for (size_t i = 0; i < count; i++)
  {
    interfaces[i] = device->interfaceInfos[i];
  }
  return count;
}

size_t EspUsbHost::getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const
{
  if (!endpoints || maxEndpoints == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->endpointInfoCount < maxEndpoints ? device->endpointInfoCount : maxEndpoints;
  for (size_t i = 0; i < count; i++)
  {
    endpoints[i] = device->endpointInfos[i];
  }
  return count;
}

size_t EspUsbHost::endpointChannelCount(uint8_t address) const
{
  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      count += device.endpointChannelCount;
    }
  }
  return count;
}

size_t EspUsbHost::managedEndpointCount(uint8_t address) const
{
  size_t count = 0;
  for (const EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || endpoint.deviceAddress == address)
    {
      count++;
    }
  }
  return count;
}

size_t EspUsbHost::ep0ChannelCount(uint8_t address) const
{
  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      count++;
    }
  }
  return count;
}

size_t EspUsbHost::hubEndpointChannelCount(uint8_t address) const
{
  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.info.isHub)
    {
      continue;
    }
    if (address != ESP_USB_HOST_ANY_ADDRESS && device.info.address != address)
    {
      continue;
    }
    count += device.endpointInfoCount;
  }
  return count;
}

size_t EspUsbHost::estimatedHcdChannelCount(uint8_t address) const
{
  return ep0ChannelCount(address) + endpointChannelCount(address) + hubEndpointChannelCount(address);
}

size_t EspUsbHost::maxEndpointChannelCount() const
{
  return 8;
}

size_t EspUsbHost::getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams, size_t maxStreams) const
{
  if (!streams || maxStreams == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->audioStreamInfoCount < maxStreams ? device->audioStreamInfoCount : maxStreams;
  for (size_t i = 0; i < count; i++)
  {
    streams[i] = device->audioStreamInfos[i];
  }
  return count;
}

void EspUsbHost::releaseAudioOutputTransfers(DeviceState &device)
{
  device.audioOutRunning = false;
  for (size_t i = 0; i < ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS; i++)
  {
    usb_transfer_t *transfer = device.audioOutTransfers[i];
    device.audioOutTransfers[i] = nullptr;
    if (transfer)
    {
      usb_host_transfer_free(transfer);
    }
  }
}

void EspUsbHost::releaseEndpoints(DeviceState &device, bool clearEndpoints)
{
  releaseAudioOutputTransfers(device);
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse || endpoint.deviceHandle != device.handle)
    {
      continue;
    }
    if (endpoint.transfer)
    {
      if (clearEndpoints && device.handle)
      {
        esp_err_t err = usb_host_endpoint_clear(device.handle, endpoint.address);
        if (err != ESP_OK)
        {
          ESP_LOGD(TAG, "usb_host_endpoint_clear(0x%02x) failed: %s",
                   endpoint.address,
                   esp_err_to_name(err));
        }
      }
      usb_host_transfer_free(endpoint.transfer);
    }
    endpoint = EndpointState();
  }
}

void EspUsbHost::releaseAllEndpoints(bool clearEndpoints)
{
  for (DeviceState &device : devices_)
  {
    if (device.inUse)
    {
      releaseEndpoints(device, clearEndpoints);
    }
  }
}

void EspUsbHost::releaseInterfaces(DeviceState &device)
{
  for (uint8_t i = 0; i < device.interfaceCount; i++)
  {
    usb_host_interface_release(clientHandle_, device.handle, device.interfaces[i]);
    device.interfaces[i] = 0;
  }
  device.interfaceCount = 0;
  device.endpointChannelCount = 0;
}

void EspUsbHost::configureCdcAcm(DeviceState &device)
{
  if (device.cdcConfigured || !device.hasCdcControlInterface || !device.handle || !clientHandle_)
  {
    return;
  }

  uint8_t lineCoding[7] = {};
  fillCdcLineCoding(device.serialConfig, lineCoding);

  usb_transfer_t *lineCodingTransfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + sizeof(lineCoding), 0, &lineCodingTransfer);
  if (err == ESP_OK)
  {
    usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(lineCodingTransfer->data_buffer);
    setup->bmRequestType = CDC_SET_REQUEST_TYPE;
    setup->bRequest = CDC_CLASS_REQUEST_SET_LINE_CODING;
    setup->wValue = 0;
    setup->wIndex = device.cdcControlInterfaceNumber;
    setup->wLength = sizeof(lineCoding);
    memcpy(lineCodingTransfer->data_buffer + USB_SETUP_PACKET_SIZE, lineCoding, sizeof(lineCoding));
    lineCodingTransfer->device_handle = device.handle;
    lineCodingTransfer->bEndpointAddress = 0;
    lineCodingTransfer->callback = controlTransferCallback;
    lineCodingTransfer->context = this;
    lineCodingTransfer->num_bytes = USB_SETUP_PACKET_SIZE + sizeof(lineCoding);
    err = usb_host_transfer_submit_control(clientHandle_, lineCodingTransfer);
    if (err != ESP_OK)
    {
      usb_host_transfer_free(lineCodingTransfer);
      setLastError(err);
    }
  }

  usb_transfer_t *lineStateTransfer = nullptr;
  err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &lineStateTransfer);
  if (err == ESP_OK)
  {
    usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(lineStateTransfer->data_buffer);
    setup->bmRequestType = CDC_SET_REQUEST_TYPE;
    setup->bRequest = CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE;
    setup->wValue = (device.serialDtr ? 0x0001 : 0) | (device.serialRts ? 0x0002 : 0);
    setup->wIndex = device.cdcControlInterfaceNumber;
    setup->wLength = 0;
    lineStateTransfer->device_handle = device.handle;
    lineStateTransfer->bEndpointAddress = 0;
    lineStateTransfer->callback = controlTransferCallback;
    lineStateTransfer->context = this;
    lineStateTransfer->num_bytes = USB_SETUP_PACKET_SIZE;
    err = usb_host_transfer_submit_control(clientHandle_, lineStateTransfer);
    if (err != ESP_OK)
    {
      usb_host_transfer_free(lineStateTransfer);
      setLastError(err);
    }
  }

  device.cdcConfigured = true;
  ESP_LOGI(TAG, "CDC ACM configured: baud=%lu dataBits=%u parity=%u stopBits=%u dtr=%u rts=%u",
           static_cast<unsigned long>(device.serialConfig.baud),
           device.serialConfig.dataBits,
           static_cast<unsigned>(device.serialConfig.parity),
           static_cast<unsigned>(device.serialConfig.stopBits),
           device.serialDtr ? 1 : 0,
           device.serialRts ? 1 : 0);
}

void EspUsbHost::attachCdcSerial(EspUsbHostCdcSerial *serial)
{
  if (!serial)
  {
    return;
  }
  for (EspUsbHostCdcSerial *existing : cdcSerials_)
  {
    if (existing == serial)
    {
      return;
    }
  }
  for (EspUsbHostCdcSerial *&slot : cdcSerials_)
  {
    if (!slot)
    {
      slot = serial;
      return;
    }
  }
  ESP_LOGW(TAG, "No CDC serial slots available");
}

void EspUsbHost::detachCdcSerial(EspUsbHostCdcSerial *serial)
{
  for (EspUsbHostCdcSerial *&slot : cdcSerials_)
  {
    if (slot == serial)
    {
      slot = nullptr;
    }
  }
}

void EspUsbHost::configureVendorSerial(DeviceState &device)
{
  if (!device.vendorSerialSupported || !device.hasVendorSerialInterface || !device.handle || !clientHandle_)
  {
    return;
  }

  ESP_LOGI(TAG, "Configuring %s VCP: iface=%u baud=%lu dataBits=%u parity=%u stopBits=%u dtr=%u rts=%u",
           vendorSerialName(device.info.vid),
           device.vendorSerialInterfaceNumber,
           static_cast<unsigned long>(device.serialConfig.baud),
           device.serialConfig.dataBits,
           static_cast<unsigned>(device.serialConfig.parity),
           static_cast<unsigned>(device.serialConfig.stopBits),
           device.serialDtr ? 1 : 0,
           device.serialRts ? 1 : 0);

  if (device.info.vid == 0x0403)
  {
    const uint16_t divisor = ftdiBaudDivisor(device.serialConfig.baud);

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x00, 0x0000, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x03, divisor, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x04, ftdiDataCharacteristics(device.serialConfig), device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, device.serialDtr ? 0x0011 : 0x0010, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, device.serialRts ? 0x0021 : 0x0020, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
  else if (device.info.vid == 0x10c4)
  {
    const uint8_t baud[4] = {
        static_cast<uint8_t>(device.serialConfig.baud & 0xff),
        static_cast<uint8_t>((device.serialConfig.baud >> 8) & 0xff),
        static_cast<uint8_t>((device.serialConfig.baud >> 16) & 0xff),
        static_cast<uint8_t>((device.serialConfig.baud >> 24) & 0xff)};
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x00, 0x0001, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x1e, 0x0000, device.vendorSerialInterfaceNumber, baud, sizeof(baud), device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x03, cp210xLineControl(device.serialConfig), device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x07,
                              (device.serialDtr ? 0x0001 : 0) | (device.serialRts ? 0x0002 : 0) | 0x0300,
                              device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
  else if (device.info.vid == 0x1a86)
  {
    const uint16_t lineControl = ch34xLineControl(device.serialConfig);
    const uint16_t baudReg = ch34xBaudValue(device.serialConfig.baud);

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa1, 0x0000, 0x0000, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x1312, baudReg, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x2518, lineControl, nullptr, 0, device.info.address);
    const uint8_t modemControl = (device.serialDtr ? 0x20 : 0) | (device.serialRts ? 0x40 : 0);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa4,
                              static_cast<uint16_t>(~modemControl),
                              device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
  else if (device.info.vid == 0x067b)
  {
    if (device.info.pid == 0x2303)
    {
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0404, 0x0000, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8383, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0404, 0x0001, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8484, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_IN_REQUEST_TYPE, VENDOR_READ_REQUEST, 0x8383, 0x0000, nullptr, 1, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0000, 0x0001, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0001, 0x0000, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0002, 0x0044, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0008, 0x0000, nullptr, 0, device.info.address);
      submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, VENDOR_WRITE_REQUEST, 0x0009, 0x0000, nullptr, 0, device.info.address);
    }

    uint8_t lineCoding[7] = {};
    fillCdcLineCoding(device.serialConfig, lineCoding);
    submitVendorSerialControl(CDC_SET_REQUEST_TYPE, CDC_CLASS_REQUEST_SET_LINE_CODING,
                              0x0000, 0x0000,
                              lineCoding, sizeof(lineCoding), device.info.address);
    submitVendorSerialControl(CDC_SET_REQUEST_TYPE, CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE,
                              (device.serialDtr ? 0x0001 : 0) | (device.serialRts ? 0x0002 : 0),
                              0x0000, nullptr, 0, device.info.address);
  }
}

bool EspUsbHost::submitSetInterface(DeviceState &device, uint8_t interfaceNumber, uint8_t alternateSetting)
{
  if (!clientHandle_ || !device.handle)
  {
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Set_Interface) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  USB_SETUP_PACKET_INIT_SET_INTERFACE(setup, interfaceNumber, alternateSetting);
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Set_Interface iface=%u alt=%u) failed: %s",
             interfaceNumber,
             alternateSetting,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "Set_Interface submitted iface=%u alt=%u", interfaceNumber, alternateSetting);
  return true;
}

bool EspUsbHost::submitAudioSamplingFrequency(DeviceState &device, uint8_t endpointAddress, uint32_t sampleRate)
{
  if (!clientHandle_ || !device.handle)
  {
    return false;
  }

  static constexpr uint8_t AUDIO_SET_CUR_REQUEST_TYPE = 0x22;
  static constexpr uint8_t AUDIO_SET_CUR_REQUEST = 0x01;
  static constexpr uint16_t AUDIO_EP_SAMPLING_FREQ_CONTROL = 0x0100;
  static constexpr size_t AUDIO_SAMPLE_RATE_LENGTH = 3;

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + AUDIO_SAMPLE_RATE_LENGTH, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Audio SET_CUR sampling frequency) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = AUDIO_SET_CUR_REQUEST_TYPE;
  setup->bRequest = AUDIO_SET_CUR_REQUEST;
  setup->wValue = AUDIO_EP_SAMPLING_FREQ_CONTROL;
  setup->wIndex = endpointAddress;
  setup->wLength = AUDIO_SAMPLE_RATE_LENGTH;

  uint8_t *frequency = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
  frequency[0] = sampleRate & 0xff;
  frequency[1] = (sampleRate >> 8) & 0xff;
  frequency[2] = (sampleRate >> 16) & 0xff;

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + AUDIO_SAMPLE_RATE_LENGTH;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Audio SET_CUR sampling frequency ep=0x%02x) failed: %s",
             endpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "Audio SET_CUR sampling frequency submitted ep=0x%02x rate=%lu",
           endpointAddress,
           static_cast<unsigned long>(sampleRate));
  return true;
}

bool EspUsbHost::audioFeatureControl(DeviceState &device,
                                     uint8_t request,
                                     uint8_t unitId,
                                     uint8_t controlSelector,
                                     uint8_t channel,
                                     uint8_t *data,
                                     size_t length,
                                     bool dataIn,
                                     uint32_t timeoutMs)
{
  if (!clientHandle_ || !device.handle || !data || length == 0 || device.audioControlInterfaceNumber == 0xff)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "USB Audio control APIs cannot run from USB client task");
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Audio feature control) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = dataIn ? 0xa1 : 0x21;
  setup->bRequest = request;
  setup->wValue = (static_cast<uint16_t>(controlSelector) << 8) | channel;
  setup->wIndex = (static_cast<uint16_t>(unitId) << 8) | device.audioControlInterfaceNumber;
  setup->wLength = length;
  if (!dataIn)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  context.status = USB_TRANSFER_STATUS_ERROR;
  context.actualLength = 0;
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Audio feature unit=%u control=%u) failed: %s",
             unitId,
             controlSelector,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  if (!done)
  {
    ESP_LOGW(TAG, "USB Audio control timeout unit=%u control=%u", unitId, controlSelector);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  const bool ok = context.status == USB_TRANSFER_STATUS_COMPLETED &&
                  (!dataIn || context.actualLength >= USB_SETUP_PACKET_SIZE + length);
  if (ok && dataIn)
  {
    memcpy(data, transfer->data_buffer + USB_SETUP_PACKET_SIZE, length);
  }
  if (!ok)
  {
    ESP_LOGW(TAG, "USB Audio control failed unit=%u control=%u status=%d actual=%u",
             unitId,
             controlSelector,
             context.status,
             static_cast<unsigned>(context.actualLength));
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  return ok;
}

bool EspUsbHost::submitVendorSerialControl(uint8_t requestType,
                                           uint8_t request,
                                           uint16_t value,
                                           uint16_t index,
                                           const uint8_t *data,
                                           size_t length,
                                           uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!device || !device->handle)
  {
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = requestType;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = index;
  setup->wLength = length;
  if (length > 0 && data)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGD(TAG, "VCP control request 0x%02x failed: %s", request, esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::submitVendorControl(DeviceState &device,
                                     uint8_t requestType,
                                     uint8_t request,
                                     uint16_t value,
                                     uint16_t index,
                                     uint8_t *data,
                                     size_t length,
                                     size_t *actualLength,
                                     uint32_t timeoutMs)
{
  if (!device.handle)
  {
    return false;
  }
  if (length > 0 && !data)
  {
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor control) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool dataIn = (requestType & 0x80) != 0;
  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = requestType;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = index;
  setup->wLength = length;
  if (!dataIn && length > 0)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  context.status = USB_TRANSFER_STATUS_ERROR;
  context.actualLength = 0;
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(vendor request=0x%02x) failed: %s",
             request,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  if (!done)
  {
    ESP_LOGW(TAG, "USB vendor control timeout request=0x%02x", request);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  size_t payloadLength = 0;
  if (context.actualLength > USB_SETUP_PACKET_SIZE)
  {
    payloadLength = context.actualLength - USB_SETUP_PACKET_SIZE;
  }
  if (payloadLength > length)
  {
    payloadLength = length;
  }
  if (actualLength)
  {
    *actualLength = dataIn ? payloadLength : 0;
  }

  const bool ok = context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (ok && dataIn && payloadLength > 0)
  {
    memcpy(data, transfer->data_buffer + USB_SETUP_PACKET_SIZE, payloadLength);
  }
  if (!ok)
  {
    ESP_LOGW(TAG, "USB vendor control failed request=0x%02x status=%d actual=%u",
             request,
             context.status,
             static_cast<unsigned>(context.actualLength));
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  return ok;
}

void EspUsbHost::setLastError(esp_err_t err)
{
  if (err != ESP_OK)
  {
    lastError_ = err;
  }
}

String EspUsbHost::usbString(const usb_str_desc_t *strDesc)
{
  String result;
  if (!strDesc)
  {
    return result;
  }
  for (int i = 0; i < strDesc->bLength / 2; i++)
  {
    if (strDesc->wData[i] <= 0xff)
    {
      result += static_cast<char>(strDesc->wData[i]);
    }
  }
  return result;
}

EspUsbHostCdcSerial::EspUsbHostCdcSerial(EspUsbHost &host) : host_(host)
{
}

bool EspUsbHostCdcSerial::begin(uint32_t baud)
{
  portENTER_CRITICAL(&rxMux_);
  rxHead_ = 0;
  rxTail_ = 0;
  portEXIT_CRITICAL(&rxMux_);

  host_.attachCdcSerial(this);
  return host_.setSerialBaudRate(baud, address_);
}

void EspUsbHostCdcSerial::end()
{
  host_.detachCdcSerial(this);
}

bool EspUsbHostCdcSerial::connected() const
{
  return host_.serialReady(address_);
}

int EspUsbHostCdcSerial::available()
{
  portENTER_CRITICAL(&rxMux_);
  const size_t count = rxHead_ >= rxTail_ ? rxHead_ - rxTail_ : RX_BUFFER_SIZE - rxTail_ + rxHead_;
  portEXIT_CRITICAL(&rxMux_);
  return static_cast<int>(count);
}

int EspUsbHostCdcSerial::read()
{
  portENTER_CRITICAL(&rxMux_);
  if (rxHead_ == rxTail_)
  {
    portEXIT_CRITICAL(&rxMux_);
    return -1;
  }
  const uint8_t value = rxBuffer_[rxTail_];
  rxTail_ = nextIndex(rxTail_);
  portEXIT_CRITICAL(&rxMux_);
  return value;
}

int EspUsbHostCdcSerial::peek()
{
  portENTER_CRITICAL(&rxMux_);
  if (rxHead_ == rxTail_)
  {
    portEXIT_CRITICAL(&rxMux_);
    return -1;
  }
  const uint8_t value = rxBuffer_[rxTail_];
  portEXIT_CRITICAL(&rxMux_);
  return value;
}

void EspUsbHostCdcSerial::flush()
{
}

size_t EspUsbHostCdcSerial::write(uint8_t data)
{
  return write(&data, 1);
}

size_t EspUsbHostCdcSerial::write(const uint8_t *buffer, size_t size)
{
  if (size == 0)
  {
    return 0;
  }
  return host_.sendSerial(buffer, size, address_) ? size : 0;
}

bool EspUsbHostCdcSerial::setBaudRate(uint32_t baud)
{
  return host_.setSerialBaudRate(baud, address_);
}

bool EspUsbHostCdcSerial::setConfig(const EspUsbHostSerialConfig &config)
{
  return host_.setSerialConfig(config, address_);
}

bool EspUsbHostCdcSerial::setDtr(bool enable)
{
  EspUsbHost::DeviceState *device = host_.findSerialDevice(address_);
  if (!device)
  {
    return false;
  }
  device->serialDtr = enable;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    host_.configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    host_.configureVendorSerial(*device);
  }
  return true;
}

bool EspUsbHostCdcSerial::setRts(bool enable)
{
  EspUsbHost::DeviceState *device = host_.findSerialDevice(address_);
  if (!device)
  {
    return false;
  }
  device->serialRts = enable;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    host_.configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    host_.configureVendorSerial(*device);
  }
  return true;
}

void EspUsbHostCdcSerial::setAddress(uint8_t address)
{
  address_ = address;
}

uint8_t EspUsbHostCdcSerial::address() const
{
  return address_;
}

void EspUsbHostCdcSerial::clearAddress()
{
  address_ = ESP_USB_HOST_ANY_ADDRESS;
}

void EspUsbHostCdcSerial::pushData(const uint8_t *data, size_t length)
{
  portENTER_CRITICAL(&rxMux_);
  for (size_t i = 0; i < length; i++)
  {
    const size_t next = nextIndex(rxHead_);
    if (next == rxTail_)
    {
      rxTail_ = nextIndex(rxTail_);
    }
    rxBuffer_[rxHead_] = data[i];
    rxHead_ = next;
  }
  portEXIT_CRITICAL(&rxMux_);
}

bool EspUsbHostCdcSerial::accepts(uint8_t address) const
{
  return address_ == ESP_USB_HOST_ANY_ADDRESS || address_ == address;
}

size_t EspUsbHostCdcSerial::nextIndex(size_t index) const
{
  index++;
  if (index >= RX_BUFFER_SIZE)
  {
    return 0;
  }
  return index;
}
