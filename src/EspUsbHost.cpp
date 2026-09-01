#include "EspUsbHost.h"
#include "EspUsbHostHid.h"

#include "diskio_impl.h"
#include "esp_vfs_fat.h"
#include "vfs_api.h"

#include <esp_idf_version.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include <new>
#include <utility>

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "hal/usb_dwc_ll.h"
#include "soc/usb_dwc_struct.h"
#endif

// Targets whose DMA-capable memory is cached (ESP32-P4 and later) need explicit
// cache maintenance around USB DMA buffers.
#if defined(CONFIG_CACHE_L1_CACHE_LINE_SIZE) && CONFIG_CACHE_L1_CACHE_LINE_SIZE > 0
#include "esp_cache.h"
#define ESP_USB_HOST_DMA_CACHE_SYNC 1
#endif

// usb_host_config_t::fifo_settings_custom arrived in ESP-IDF 5.5 (arduino-esp32
// 3.3.0). Before that the FIFO split could only be chosen through the Kconfig
// bias, which is baked into the precompiled core libraries.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#define ESP_USB_HOST_HAS_FIFO_SETTINGS 1
#endif

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
static const char *TAG = "EspUsbHost";
#endif

static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t USB_CLASS_AUDIO_VALUE = 0x01;
static constexpr uint8_t USB_CLASS_CDC_CONTROL_VALUE = 0x02;
static constexpr uint8_t USB_CLASS_CDC_DATA_VALUE = 0x0a;
static constexpr uint8_t USB_CLASS_MASS_STORAGE_VALUE = 0x08;
static constexpr uint8_t USB_CLASS_VENDOR_VALUE = 0xff;
static constexpr uint8_t USB_CLASS_CCID_VALUE = 0x0b;
static constexpr uint8_t USB_CS_INTERFACE_DESC = 0x24;
// Aliased rather than repeated: the MIDI cable decoder in the header validates
// the same descriptor type, and the two must not drift apart.
static constexpr uint8_t USB_CS_ENDPOINT_DESC = ESP_USB_HOST_MIDI_CS_ENDPOINT;
static constexpr uint8_t USB_CDC_SUBCLASS_ECM = 0x06;
static constexpr uint8_t USB_CDC_SUBCLASS_NCM = 0x0d;
static constexpr uint8_t USB_CDC_SUBCLASS_ACM = 0x02;
static constexpr uint8_t USB_CDC_CS_UNION = 0x06;
static constexpr uint8_t USB_CDC_CS_ETHERNET = 0x0f;
static constexpr uint8_t USB_CDC_CS_NCM = 0x1a;
// CDC NCM class-specific requests (NCM 1.0 table 6-2).
static constexpr uint8_t USB_CDC_REQ_GET_NTB_PARAMETERS = 0x80;
static constexpr uint8_t USB_CDC_REQ_SET_NTB_INPUT_SIZE = 0x86;
// bmNetworkCapabilities bit 3: SetNtbInputSize / GetNtbInputSize supported.
static constexpr uint8_t USB_CDC_NCM_CAP_NTB_INPUT_SIZE = 0x08;
// NTB parameter structure length (NCM 1.0 table 6-3).
static constexpr size_t USB_CDC_NCM_NTB_PARAM_LEN = 28;
static constexpr uint8_t USB_AUDIO_SUBCLASS_AUDIO_CONTROL = 0x01;
static constexpr uint8_t USB_AUDIO_SUBCLASS_AUDIO_STREAMING = 0x02;
static constexpr uint8_t USB_AUDIO_SUBCLASS_MIDI_STREAMING = 0x03;
static constexpr uint8_t USB_AUDIO_AC_HEADER = 0x01;
static constexpr uint8_t USB_AUDIO_AC_INPUT_TERMINAL = 0x02;
static constexpr uint8_t USB_AUDIO_AC_OUTPUT_TERMINAL = 0x03;
static constexpr uint8_t USB_AUDIO_AC_FEATURE_UNIT = 0x06;
static constexpr uint8_t USB_AUDIO_AC_CLOCK_SOURCE = 0x0a;
static constexpr uint8_t USB_AUDIO_FEATURE_MUTE_CONTROL = 0x01;
static constexpr uint8_t USB_AUDIO_FEATURE_VOLUME_CONTROL = 0x02;
// UAC2 class requests and control selectors. RANGE replaces the UAC1
// GET_MIN/GET_MAX/GET_RES triple, and the sample frequency moved from the
// endpoint to the Clock Source entity's SAM_FREQ control.
static constexpr uint8_t USB_AUDIO_REQUEST_CUR = 0x01;
static constexpr uint8_t USB_AUDIO_REQUEST_RANGE = 0x02;
static constexpr uint8_t USB_AUDIO_CLOCK_SAM_FREQ_CONTROL = 0x01;
static constexpr uint8_t USB_AUDIO_ENTITY_SET_REQUEST_TYPE = 0x21;
static constexpr uint8_t USB_AUDIO_ENTITY_GET_REQUEST_TYPE = 0xa1;
// Clock Source bmControls D1..D0: 01 = sample frequency readable, 11 = also
// host-programmable.
static constexpr uint8_t USB_AUDIO_CLOCK_FREQ_CONTROL_MASK = 0x03;
static constexpr uint8_t USB_AUDIO_CLOCK_FREQ_CONTROL_RW = 0x03;
// UAC1 encodes the direction in bRequest (GET_CUR = 0x81, SET_CUR = 0x01). UAC2
// has one code per attribute (CUR = 0x01, RANGE = 0x02) and takes the direction
// from bmRequestType, so a UAC2 device stalls the UAC1 GET codes.
static constexpr uint8_t USB_AUDIO_UAC1_GET_CUR_REQUEST = 0x81;

static uint8_t audioCurRequest(uint8_t protocol, bool dataIn)
{
  if (!dataIn || protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    return USB_AUDIO_REQUEST_CUR;
  }
  return USB_AUDIO_UAC1_GET_CUR_REQUEST;
}
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
static constexpr uint8_t USB_AUDIO_CS_AS_GENERAL = 0x01;
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

enum EspUsbHostVendorTransferState : uint8_t
{
  ESP_USB_HOST_VENDOR_TRANSFER_WAITING = 0,
  ESP_USB_HOST_VENDOR_TRANSFER_CALLBACK = 1,
  ESP_USB_HOST_VENDOR_TRANSFER_ABANDONED = 2,
};

struct EspUsbHostVendorTransferContext
{
  SemaphoreHandle_t done = nullptr;
  usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
  size_t actualLength = 0;
  std::atomic<uint8_t> state{ESP_USB_HOST_VENDOR_TRANSFER_WAITING};
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

// attemptIndex sentinel for the GET CUR fallback of the UAC2 sample frequency
// query. Kept above every RANGE subrange count so the two never collide.
static constexpr uint8_t AUDIO_CLOCK_CUR_ATTEMPT = 0xff;

struct AudioClockRangeTransferContext
{
  EspUsbHost *host = nullptr;
  uint8_t address = 0;
  uint8_t clockSourceId = 0;
  uint8_t attemptIndex = 0;
};

// The first SAM_FREQ RANGE request asks for a single subrange rather than doing
// the spec's 2-byte wNumSubRanges probe first. Devices that validate wLength
// against their own subrange count stall the probe, and a single discrete rate is
// the common case, so this usually completes in one request without a stall. Any
// device that has more subranges still says so in the wNumSubRanges it returns,
// which the callback uses to ask again for the exact size.
static constexpr uint8_t AUDIO_CLOCK_FIRST_ATTEMPT = 1;

// Retry order after a failed attempt: one subrange, then the probe, then the
// remaining exact sizes, then GET CUR.
static uint8_t nextAudioClockRangeAttempt(uint8_t attempt)
{
  if (attempt == AUDIO_CLOCK_FIRST_ATTEMPT)
  {
    return 0;
  }
  const uint8_t next = attempt == 0 ? 2 : static_cast<uint8_t>(attempt + 1);
  return next > ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES ? AUDIO_CLOCK_CUR_ATTEMPT : next;
}

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

static void vendorTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHostVendorTransferContext *context = static_cast<EspUsbHostVendorTransferContext *>(transfer->context);
  if (!context)
  {
    return;
  }
  context->status = transfer->status;
  context->actualLength = transfer->actual_num_bytes;
  const uint8_t previous = context->state.exchange(ESP_USB_HOST_VENDOR_TRANSFER_CALLBACK,
                                                   std::memory_order_acq_rel);
  if (previous == ESP_USB_HOST_VENDOR_TRANSFER_ABANDONED)
  {
    vSemaphoreDelete(context->done);
    usb_host_transfer_free(transfer);
    delete context;
    return;
  }
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

// ---------------------------------------------------------------------------
// CDC-NCM (USB network) helpers
// ---------------------------------------------------------------------------

#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
#include <esp_netif.h>
#include <esp_netif_defaults.h>
#include <esp_event.h>
#endif

// NCM 1.0, 16-bit NTB. Signatures are stored little-endian on the wire.
static constexpr uint32_t ESP_USB_HOST_NCM_NTH16_SIG = 0x484D434E; // "NCMH"
static constexpr uint32_t ESP_USB_HOST_NCM_NDP16_SIG = 0x304D434E; // "NCM0"
static constexpr uint16_t ESP_USB_HOST_NCM_NTH16_LEN = 12;
static constexpr uint16_t ESP_USB_HOST_NCM_NDP16_MIN_LEN = 16; // header(8) + one datagram entry + null entry
// CDC notification codes (bNotificationCode) on the interrupt IN endpoint.
static constexpr uint8_t ESP_USB_HOST_CDC_NOTIFY_NETWORK_CONNECTION = 0x00;
static constexpr uint8_t ESP_USB_HOST_CDC_NOTIFY_SPEED_CHANGE = 0x2a;

static inline uint16_t ncmRead16(const uint8_t *p)
{
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t ncmRead32(const uint8_t *p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static inline void ncmWrite16(uint8_t *p, uint16_t v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
}

static inline void ncmWrite32(uint8_t *p, uint32_t v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

static inline size_t ncmAlign4(size_t v)
{
  return (v + 3u) & ~static_cast<size_t>(3u);
}

#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
// One driver context per attached USB network interface. Its first member is
// the esp_netif driver base so esp_netif's handle == &ctx. The trampolines
// recover the owning EspUsbHost + device address from it.
struct EspUsbHostNetifDriver
{
  esp_netif_driver_base_t base;
  EspUsbHost *host;
  uint8_t address;
};

static esp_err_t espUsbHostNetifPostAttach(esp_netif_t *netif, esp_netif_iodriver_handle h)
{
  EspUsbHostNetifDriver *driver = static_cast<EspUsbHostNetifDriver *>(h);
  driver->base.netif = netif;
  return ESP_OK;
}

static void espUsbHostNetifFreeRx(void *h, void *buffer)
{
  (void)h;
  free(buffer);
}

static esp_err_t espUsbHostNetifTransmit(void *h, void *buffer, size_t length)
{
  EspUsbHostNetifDriver *driver = static_cast<EspUsbHostNetifDriver *>(h);
  if (!driver || !driver->host)
  {
    return ESP_ERR_INVALID_STATE;
  }
  // esp_netif hands us a complete Ethernet frame; networkWriteFrame() wraps it
  // in an NTB (copying into its own transfer buffer, so `buffer` need not
  // outlive this call) and sends it synchronously over bulk OUT.
  return driver->host->networkWriteFrame(static_cast<const uint8_t *>(buffer), length, driver->address)
             ? ESP_OK
             : ESP_FAIL;
}
#endif

// Write back the CPU cache lines covering an IN transfer's DMA buffer before it
// is submitted.
//
// ESP-IDF's HCD only synchronizes an IN buffer when the transfer completes
// (M2C invalidate in hcd_urb_dequeue()); it never writes back before the DMA
// starts. usb_host_transfer_alloc() zeroes the buffer through the cache, and
// the allocator writes its own bookkeeping into the same memory, so a freshly
// allocated transfer carries dirty lines. Evicting one of them while the
// controller is writing puts stale CPU data on top of received data. On
// ESP32-P4 this reproduces as a fixed 16-byte block of wrong bytes inside
// otherwise correct sectors (tests/manual/msc_cache_coherency). MSC reads are
// hit hardest because every command allocates a fresh, fully dirty buffer.
//
// The buffer belongs to the transfer alone (IDF aligns it to the cache line),
// so writing it back here cannot disturb anything else, and clean lines make
// the call nearly free on later submits.
static void espUsbHostCacheSyncBeforeInTransfer(usb_transfer_t *transfer)
{
#if defined(ESP_USB_HOST_DMA_CACHE_SYNC)
  if (!transfer || !transfer->data_buffer || transfer->data_buffer_size == 0)
  {
    return;
  }
  const esp_err_t err = esp_cache_msync(transfer->data_buffer,
                                        transfer->data_buffer_size,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_cache_msync(IN buffer) failed: %s", esp_err_to_name(err));
  }
#else
  (void)transfer;
#endif
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

#if defined(CONFIG_IDF_TARGET_ESP32P4)
static unsigned hostPeripheralMap(EspUsbHostPort port)
{
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
}
#endif

#if defined(ESP_USB_HOST_HAS_FIFO_SETTINGS)
// Total FIFO the controller has to divide up, in lines of 4 bytes: 4 kB behind a
// high-speed PHY, 1 kB behind a full-speed one. The hardware keeps a few lines
// at the end for endpoint bookkeeping, so a split can still be rejected by the
// host driver slightly below this; it is only used to catch a config that cannot
// possibly fit before a device gets plugged in.
static uint32_t hostFifoCapacityLines(EspUsbHostPort port)
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return (port == ESP_USB_HOST_PORT_FULL_SPEED) ? 256 : 1024;
#else
  (void)port;
  return 256;
#endif
}
#endif

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

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
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
  case 0x07:
    return "Printer"; // ESC/POS receipt printers, IPP/raw printers
  case 0x08:
    return "Mass Storage";
  case 0x09:
    return "Hub";
  case 0x0a:
    return "CDC Data";
  case 0x0b:
    return "Smart Card"; // CCID
  case 0x0e:
    return "Video";
  case 0xfe:
    return "Application Specific"; // USBTMC, DFU, IrDA bridge
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

const char *espUsbHostNetworkProtocolName(EspUsbHostNetworkProtocol protocol)
{
  switch (protocol)
  {
  case ESP_USB_HOST_NETWORK_PROTOCOL_CDC_ECM:
    return "CDC-ECM";
  case ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM:
    return "CDC-NCM";
  case ESP_USB_HOST_NETWORK_PROTOCOL_NONE:
  default:
    return "none";
  }
}

void espUsbHostPrint(const EspUsbHostNetworkInterfaceInfo &network, Print &out)
{
  out.printf("network: address=%u config=%u protocol=%s control_iface=%u data_iface=%u data_alt=%u mac_str=%u max_segment=%u notify_ep=0x%02x in_ep=0x%02x out_ep=0x%02x in_max=%u out_max=%u complete=%s\n",
             network.address,
             network.configurationValue,
             espUsbHostNetworkProtocolName(network.protocol),
             network.controlInterfaceNumber,
             network.dataInterfaceNumber,
             network.dataInterfaceAlternate,
             network.macAddressStringIndex,
             network.maxSegmentSize,
             network.notificationEndpoint,
             network.inEndpoint,
             network.outEndpoint,
             network.inMaxPacketSize,
             network.outMaxPacketSize,
             yesNo(network.complete()));
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
  out.printf("audio stream: addr=%u iface=%u alt=%u ep=0x%02x dir=%s channels=%u bytes=%u bits=%u rate=%lu rates=%u max_packet=%u interval=%u proto=%s clock=%u startable=%u\n",
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
             stream.interval,
             stream.protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 ? "UAC2" : "UAC1",
             stream.clockSourceId,
             stream.startable ? 1 : 0);
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

template <typename Callback, typename Event>
static void invokeHIDCallbacks(std::shared_ptr<Callback> &single,
                               std::shared_ptr<Callback> *listeners,
                               size_t listenerCount,
                               const Event &event)
{
  if (single && *single)
  {
    (*single)(event);
  }
  for (size_t i = 0; i < listenerCount; i++)
  {
    if (listeners[i] && *listeners[i])
    {
      (*listeners[i])(event);
    }
  }
}

EspUsbHost::EspUsbHost()
{
  hidCallbackMutex_ = xSemaphoreCreateMutex();
}

#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
EspUsbHost *EspUsbHost::enumerationHost_ = nullptr;

bool EspUsbHost::enumerationFilterCallback(const usb_device_desc_t *deviceDescriptor,
                                           uint8_t *configurationValue)
{
  EspUsbHost *host = enumerationHost_;
  if (!host || !deviceDescriptor || !configurationValue || !host->configurationSelector_)
  {
    return true;
  }

  const uint8_t selected = host->configurationSelector_(*deviceDescriptor);
  if (selected == 0)
  {
    return true;
  }
  if (selected > deviceDescriptor->bNumConfigurations)
  {
    ESP_LOGW(TAG, "Configuration selector returned invalid value=%u for %04x:%04x (count=%u)",
             selected,
             deviceDescriptor->idVendor,
             deviceDescriptor->idProduct,
             deviceDescriptor->bNumConfigurations);
    return true;
  }

  *configurationValue = selected;
  ESP_LOGI(TAG, "Selecting USB configuration=%u for %04x:%04x",
           selected,
           deviceDescriptor->idVendor,
           deviceDescriptor->idProduct);
  return true;
}
#endif

EspUsbHost::~EspUsbHost()
{
  end();
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  if (enumerationHost_ == this)
  {
    enumerationHost_ = nullptr;
  }
#endif
  if (hidCallbackMutex_)
  {
    vSemaphoreDelete(hidCallbackMutex_);
    hidCallbackMutex_ = nullptr;
  }
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
  if (taskHandle_ || clientTaskHandle_ || clientHandle_)
  {
    ESP_LOGW(TAG, "begin() called while USB Host shutdown is incomplete");
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }

  if (config.experimentalForceFullSpeed)
  {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    if (config.port == ESP_USB_HOST_PORT_FULL_SPEED)
    {
      ESP_LOGE(TAG, "experimentalForceFullSpeed requires the ESP32-P4 high-speed port");
      setLastError(ESP_ERR_INVALID_ARG);
      return false;
    }
#else
    ESP_LOGE(TAG, "experimentalForceFullSpeed is only supported on ESP32-P4");
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
#endif
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

bool EspUsbHost::setConfigurationSelector(ConfigurationSelector selector)
{
  if (running_)
  {
    ESP_LOGW(TAG, "setConfigurationSelector() must be called before begin()");
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  configurationSelector_ = std::move(selector);
  return true;
#else
  if (selector)
  {
    ESP_LOGW(TAG, "Configuration selection is not enabled by this Arduino-ESP32 core");
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
  }
  configurationSelector_ = ConfigurationSelector();
  return true;
#endif
}

void EspUsbHost::end()
{
  if (!running_ && !taskHandle_)
  {
    return;
  }

  const TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  if (currentTask == taskHandle_ || currentTask == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "end() cannot wait for shutdown from a USB Host task");
    setLastError(ESP_ERR_INVALID_STATE);
    return;
  }

  // A mounted volume holds a FatFs drive slot, a registered VFS path and a
  // mscFatMounts entry, and none of them survive the block device going away.
  // Do it before the tasks stop so SYNCHRONIZE CACHE can still reach the
  // device; leaving them behind makes the next mscMount() of the same basePath
  // fail with "already mounted" and runs out of drive slots after FF_VOLUMES
  // cycles.
  mscUnmountAll();

  ESP_LOGI(TAG, "Stopping USB Host");
  ready_ = false;
  running_ = false;
  const esp_err_t unblockErr = usb_host_lib_unblock();
  if (unblockErr != ESP_OK && unblockErr != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGW(TAG, "usb_host_lib_unblock() failed: %s", esp_err_to_name(unblockErr));
    setLastError(unblockErr);
  }

  const uint32_t startedAtMs = millis();
  while (taskHandle_ && millis() - startedAtMs < 3000)
  {
    delay(1);
  }
  if (taskHandle_)
  {
    ESP_LOGW(TAG, "USB Host shutdown timed out; tasks were left alive to avoid freeing in-flight transfers");
    setLastError(ESP_ERR_TIMEOUT);
  }
}

bool EspUsbHost::ready() const
{
  return ready_;
}

void EspUsbHost::onDeviceConnected(DeviceCallback callback)
{
  setHIDCallback(deviceConnectedCallback_, std::move(callback));
}

void EspUsbHost::onDeviceDisconnected(DeviceCallback callback)
{
  setHIDCallback(deviceDisconnectedCallback_, std::move(callback));
}

void EspUsbHost::onKeyboard(KeyboardCallback callback)
{
  setHIDCallback(keyboardCallback_, std::move(callback));
}

void EspUsbHost::onKeyboardState(KeyboardStateCallback callback)
{
  setHIDCallback(keyboardStateCallback_, std::move(callback));
}

void EspUsbHost::onMouse(MouseCallback callback)
{
  setHIDCallback(mouseCallback_, std::move(callback));
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
  setHIDCallback(midiMessageCallback_, std::move(callback));
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
  setHIDCallback(consumerControlCallback_, std::move(callback));
}

void EspUsbHost::onGamepad(GamepadCallback callback)
{
  setHIDCallback(gamepadCallback_, std::move(callback));
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
  setHIDCallback(systemControlCallback_, std::move(callback));
}

void EspUsbHost::onNetworkFrame(NetworkFrameCallback callback)
{
  networkFrameCallback_ = callback;
}

template <typename Callback>
void EspUsbHost::setHIDCallback(std::shared_ptr<Callback> &target, Callback callback)
{
  std::shared_ptr<Callback> stored;
  if (callback)
  {
    stored = std::make_shared<Callback>(std::move(callback));
  }
  if (!hidCallbackMutex_)
  {
    target = std::move(stored);
    return;
  }
  xSemaphoreTake(hidCallbackMutex_, portMAX_DELAY);
  target = std::move(stored);
  xSemaphoreGive(hidCallbackMutex_);
}

template <typename Callback, size_t Capacity>
EspUsbHostListenerId EspUsbHost::addHIDListener(ListenerRegistry<Callback, Capacity> &registry, Callback callback)
{
  if (!callback || !hidCallbackMutex_)
  {
    return ESP_USB_HOST_INVALID_LISTENER_ID;
  }
  std::shared_ptr<Callback> stored = std::make_shared<Callback>(std::move(callback));

  xSemaphoreTake(hidCallbackMutex_, portMAX_DELAY);
  if (registry.count >= Capacity)
  {
    xSemaphoreGive(hidCallbackMutex_);
    return ESP_USB_HOST_INVALID_LISTENER_ID;
  }

  const EspUsbHostListenerId listenerId = allocateListenerIdLocked();
  if (listenerId == ESP_USB_HOST_INVALID_LISTENER_ID)
  {
    xSemaphoreGive(hidCallbackMutex_);
    return ESP_USB_HOST_INVALID_LISTENER_ID;
  }
  ListenerSlot<Callback> &slot = registry.slots[registry.count++];
  slot.id = listenerId;
  slot.callback = std::move(stored);
  xSemaphoreGive(hidCallbackMutex_);
  return listenerId;
}

template <typename Callback, size_t Capacity>
bool EspUsbHost::removeHIDListenerLocked(ListenerRegistry<Callback, Capacity> &registry,
                                         EspUsbHostListenerId listenerId)
{
  for (size_t i = 0; i < registry.count; i++)
  {
    if (registry.slots[i].id != listenerId)
    {
      continue;
    }
    for (size_t j = i + 1; j < registry.count; j++)
    {
      registry.slots[j - 1] = std::move(registry.slots[j]);
    }
    registry.count--;
    registry.slots[registry.count] = ListenerSlot<Callback>();
    return true;
  }
  return false;
}

template <typename Callback, size_t Capacity>
bool EspUsbHost::listenerIdInUseLocked(const ListenerRegistry<Callback, Capacity> &registry,
                                       EspUsbHostListenerId listenerId) const
{
  for (size_t i = 0; i < registry.count; i++)
  {
    if (registry.slots[i].id == listenerId)
    {
      return true;
    }
  }
  return false;
}

bool EspUsbHost::listenerIdInUseLocked(EspUsbHostListenerId listenerId) const
{
  return listenerIdInUseLocked(keyboardListeners_, listenerId) ||
         listenerIdInUseLocked(keyboardStateListeners_, listenerId) ||
         listenerIdInUseLocked(mouseListeners_, listenerId) ||
         listenerIdInUseLocked(consumerControlListeners_, listenerId) ||
         listenerIdInUseLocked(systemControlListeners_, listenerId) ||
         listenerIdInUseLocked(gamepadListeners_, listenerId) ||
         listenerIdInUseLocked(midiMessageListeners_, listenerId) ||
         listenerIdInUseLocked(deviceConnectedListeners_, listenerId) ||
         listenerIdInUseLocked(deviceDisconnectedListeners_, listenerId);
}

EspUsbHostListenerId EspUsbHost::allocateListenerIdLocked()
{
  EspUsbHostListenerId candidate = nextListenerId_;
  do
  {
    if (candidate == ESP_USB_HOST_INVALID_LISTENER_ID)
    {
      candidate = 1;
    }
    if (!listenerIdInUseLocked(candidate))
    {
      nextListenerId_ = candidate + 1;
      if (nextListenerId_ == ESP_USB_HOST_INVALID_LISTENER_ID)
      {
        nextListenerId_ = 1;
      }
      return candidate;
    }
    candidate++;
  } while (candidate != nextListenerId_);
  return ESP_USB_HOST_INVALID_LISTENER_ID;
}

template <typename Callback, size_t Capacity>
size_t EspUsbHost::snapshotHIDCallbacks(const std::shared_ptr<Callback> &single,
                                        const ListenerRegistry<Callback, Capacity> &registry,
                                        std::shared_ptr<Callback> &singleSnapshot,
                                        std::shared_ptr<Callback> *listenerSnapshots)
{
  if (!hidCallbackMutex_)
  {
    singleSnapshot = single;
    for (size_t i = 0; i < registry.count; i++)
    {
      listenerSnapshots[i] = registry.slots[i].callback;
    }
    return registry.count;
  }

  xSemaphoreTake(hidCallbackMutex_, portMAX_DELAY);
  singleSnapshot = single;
  const size_t count = registry.count;
  for (size_t i = 0; i < count; i++)
  {
    listenerSnapshots[i] = registry.slots[i].callback;
  }
  xSemaphoreGive(hidCallbackMutex_);
  return count;
}

EspUsbHostListenerId EspUsbHost::addKeyboardListener(KeyboardCallback callback)
{
  return addHIDListener(keyboardListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addKeyboardStateListener(KeyboardStateCallback callback)
{
  return addHIDListener(keyboardStateListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addMouseListener(MouseCallback callback)
{
  return addHIDListener(mouseListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addConsumerControlListener(ConsumerControlCallback callback)
{
  return addHIDListener(consumerControlListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addSystemControlListener(SystemControlCallback callback)
{
  return addHIDListener(systemControlListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addGamepadListener(GamepadCallback callback)
{
  return addHIDListener(gamepadListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addDeviceConnectedListener(DeviceCallback callback)
{
  return addHIDListener(deviceConnectedListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addDeviceDisconnectedListener(DeviceCallback callback)
{
  return addHIDListener(deviceDisconnectedListeners_, std::move(callback));
}

EspUsbHostListenerId EspUsbHost::addMidiMessageListener(MidiMessageCallback callback)
{
  return addHIDListener(midiMessageListeners_, std::move(callback));
}

// Connect is reported from two places — normal enumeration and the hub address
// scan that picks up devices the enumeration event missed — so both go through
// here to keep the two paths from drifting apart.
void EspUsbHost::dispatchDeviceConnected(const EspUsbHostDeviceInfo &info)
{
  std::shared_ptr<DeviceCallback> singleCallback;
  std::shared_ptr<DeviceCallback> listeners[ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS];
  const size_t listenerCount =
      snapshotHIDCallbacks(deviceConnectedCallback_, deviceConnectedListeners_, singleCallback, listeners);
  if (singleCallback)
  {
    (*singleCallback)(info);
  }
  for (size_t i = 0; i < listenerCount; i++)
  {
    (*listeners[i])(info);
  }
}

void EspUsbHost::dispatchDeviceDisconnected(const EspUsbHostDeviceInfo &info)
{
  std::shared_ptr<DeviceCallback> singleCallback;
  std::shared_ptr<DeviceCallback> listeners[ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS];
  const size_t listenerCount =
      snapshotHIDCallbacks(deviceDisconnectedCallback_, deviceDisconnectedListeners_, singleCallback, listeners);
  if (singleCallback)
  {
    (*singleCallback)(info);
  }
  for (size_t i = 0; i < listenerCount; i++)
  {
    (*listeners[i])(info);
  }
}

bool EspUsbHost::removeListener(EspUsbHostListenerId listenerId)
{
  if (listenerId == ESP_USB_HOST_INVALID_LISTENER_ID || !hidCallbackMutex_)
  {
    return false;
  }

  xSemaphoreTake(hidCallbackMutex_, portMAX_DELAY);
  bool removed = removeHIDListenerLocked(keyboardListeners_, listenerId);
  if (!removed)
  {
    removed = removeHIDListenerLocked(keyboardStateListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(mouseListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(consumerControlListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(systemControlListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(gamepadListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(midiMessageListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(deviceConnectedListeners_, listenerId);
  }
  if (!removed)
  {
    removed = removeHIDListenerLocked(deviceDisconnectedListeners_, listenerId);
  }
  xSemaphoreGive(hidCallbackMutex_);
  return removed;
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

bool EspUsbHost::keyboardUsesBitmapReport(uint8_t address) const
{
  const DeviceState *device = findKeyboardDevice(address);
  return device && device->keyboardBitmapReport;
}

void EspUsbHost::setHubTrackingEnabled(bool enabled)
{
  hubTrackingEnabled_ = enabled;
  if (enabled)
  {
    return;
  }
  // Let go of any hub already being tracked, so the switch also works as a way out
  // once a hub has turned out to be a problem. This runs the ordinary disconnect
  // path, which closes the handle and reports the hub as gone.
  for (DeviceState &device : devices_)
  {
    if (device.inUse && device.isHub && device.handle)
    {
      ESP_LOGI(TAG, "Releasing tracked hub address=%u: hub tracking disabled", device.info.address);
      handleDeviceGone(device.handle);
    }
  }
}

bool EspUsbHost::hubTrackingEnabled() const
{
  return hubTrackingEnabled_;
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

bool EspUsbHost::networkOpen(uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!device || !device->handle)
  {
    ESP_LOGW(TAG, "networkOpen() device not found address=%u", address);
    return false;
  }

  EspUsbHostNetworkInterfaceInfo networks[ESP_USB_HOST_MAX_NETWORK_INTERFACES];
  const size_t count = getNetworkInterfaces(device->info.address, networks, ESP_USB_HOST_MAX_NETWORK_INTERFACES);
  int selected = -1;
  for (size_t i = 0; i < count; i++)
  {
    if (!networks[i].complete() || networks[i].configurationValue != device->info.configurationValue)
    {
      continue;
    }
    if (selected < 0 ||
        (networks[i].protocol == ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM &&
         networks[selected].protocol != ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM))
    {
      selected = static_cast<int>(i);
    }
  }

  if (selected < 0)
  {
    ESP_LOGW(TAG, "networkOpen() no complete CDC-ECM/CDC-NCM candidate in active configuration %u",
             device->info.configurationValue);
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
  }

  return networkOpen(networks[selected]);
}

bool EspUsbHost::networkOpen(const EspUsbHostNetworkInterfaceInfo &network)
{
  if (!running_ || !clientHandle_)
  {
    ESP_LOGW(TAG, "networkOpen() called before USB Host is ready");
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "networkOpen() cannot run from USB client task");
    return false;
  }
  if (!network.complete())
  {
    ESP_LOGW(TAG, "networkOpen() incomplete network candidate");
    return false;
  }

  DeviceState *device = findDevice(network.address);
  if (!device || !device->handle)
  {
    ESP_LOGW(TAG, "networkOpen() device not found address=%u", network.address);
    return false;
  }
  if (network.configurationValue != device->info.configurationValue)
  {
    ESP_LOGW(TAG, "networkOpen() candidate config=%u is not active config=%u",
             network.configurationValue,
             device->info.configurationValue);
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
  }

  if (device->hasNetworkInterface &&
      device->networkInterface.configurationValue == network.configurationValue &&
      device->networkInterface.controlInterfaceNumber == network.controlInterfaceNumber &&
      device->networkInterface.dataInterfaceNumber == network.dataInterfaceNumber &&
      device->networkInterface.dataInterfaceAlternate == network.dataInterfaceAlternate)
  {
    return true;
  }

  releaseNetworkInterface(*device);
  return claimNetworkInterface(*device, network);
}

void EspUsbHost::networkClose(uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!device)
  {
    return;
  }
  releaseNetworkInterface(*device);
}

bool EspUsbHost::networkReady(uint8_t address) const
{
  const DeviceState *device = findDevice(address);
  return device && device->hasNetworkInterface && device->networkInterface.complete();
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

bool EspUsbHost::deviceHasKeyboard(const DeviceState &device)
{
  return device.hasKeyboardInterface || device.keyboardLayoutInterface != 0xff;
}

bool EspUsbHost::keyboardLedTarget(const DeviceState &device, uint8_t &interfaceNumber, uint8_t &reportId)
{
  if (device.hasKeyboardInterface)
  {
    interfaceNumber = device.keyboardInterfaceNumber;
    reportId = 0;
    return true;
  }
  if (device.hasKeyboardLedOutput)
  {
    interfaceNumber = device.keyboardLedInterface;
    reportId = device.keyboardLedReportId;
    return true;
  }
  return false;
}

bool EspUsbHost::sendKeyboardLedReport(DeviceState &device, uint8_t leds)
{
  if (device.keyboardLedPending)
  {
    return false;
  }
  uint8_t interfaceNumber = 0;
  uint8_t reportId = 0;
  if (!keyboardLedTarget(device, interfaceNumber, reportId))
  {
    ESP_LOGW(TAG, "no keyboard LED output report known for address=%u", device.info.address);
    return false;
  }
  // For numbered reports, prefix the control data stage with the report ID, as
  // Linux and Windows do. TinyUSB-based devices strip a leading byte that equals
  // the report ID; without the prefix, an LED byte that happens to match it
  // (Num Lock = 0x01 with report ID 1) would be mis-stripped to an empty report.
  uint8_t payload[2];
  size_t payloadLength = 0;
  if (reportId != 0)
  {
    payload[payloadLength++] = reportId;
  }
  payload[payloadLength++] = leds;
  if (sendHIDReport(interfaceNumber,
                    ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                    reportId,
                    payload,
                    payloadLength,
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

// Whether vendorOpen() may take this interface.
//
// Automatic selection (interfaceNumber == 0xff) stays restricted to
// vendor-specific interfaces, so it can never wander into one that another part
// of this library drives. An interface the caller names explicitly is taken
// whatever its class: a bulk protocol does not have to sit behind class 0xff,
// and some devices put one behind a class code that is neither vendor-specific
// nor anything with a standard meaning here -- an AX206 USB display declares
// 0xdc / 0xa0 / 0xb0, for instance. An interface already claimed elsewhere is
// still refused; the exception is the one this device already has open, so that
// re-opening stays idempotent.
bool EspUsbHost::vendorInterfaceEligible(const DeviceState &device,
                                         const EspUsbHostInterfaceInfo &intf,
                                         uint8_t interfaceNumber) const
{
  if (interfaceNumber == 0xff)
  {
    return intf.interfaceClass == USB_CLASS_VENDOR_VALUE;
  }
  if (intf.number != interfaceNumber)
  {
    return false;
  }
  if (!intf.claimed)
  {
    return true;
  }
  return device.hasUsbVendorInterface && device.usbVendorInterfaceNumber == intf.number;
}

bool EspUsbHost::vendorOpen(uint8_t address, uint8_t interfaceNumber, EspUsbHostVendorReadMode readMode)
{
  DeviceState *device = findUsbVendorCandidate(address, interfaceNumber);
  if (!device)
  {
    ESP_LOGW(TAG, "vendorOpen() no bulk interface to claim");
    return false;
  }

  uint8_t selectedInterface = interfaceNumber;
  EspUsbHostEndpointInfo inEndpoint;
  EspUsbHostEndpointInfo outEndpoint;
  bool found = false;
  bool foundIn = false;

  // A bulk IN / bulk OUT pair is preferred, but an interface that only exposes
  // a bulk OUT endpoint is still usable for vendorWrite(). USB graphics
  // adapters, for example, pair their bulk OUT with an interrupt IN that this
  // API does not use. Remember the first such interface as a fallback.
  uint8_t outOnlyInterface = 0;
  EspUsbHostEndpointInfo outOnlyEndpoint;
  bool outOnlyFound = false;

  for (uint8_t i = 0; i < device->interfaceInfoCount; i++)
  {
    const EspUsbHostInterfaceInfo &intf = device->interfaceInfos[i];
    if (!vendorInterfaceEligible(*device, intf, interfaceNumber))
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
      // An interface can expose more than one bulk endpoint per direction; a
      // USB graphics adapter, for example, has two bulk OUT endpoints. Keep the
      // first one in descriptor order so the choice is predictable.
      if (ep.address & 0x80)
      {
        if (!hasIn)
        {
          candidateIn = ep;
          hasIn = true;
        }
      }
      else if (!hasOut)
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
      foundIn = true;
      break;
    }
    if (hasOut && !outOnlyFound)
    {
      outOnlyInterface = intf.number;
      outOnlyEndpoint = candidateOut;
      outOnlyFound = true;
    }
  }

  if (!found && outOnlyFound)
  {
    selectedInterface = outOnlyInterface;
    outEndpoint = outOnlyEndpoint;
    found = true;
  }

  if (!found)
  {
    ESP_LOGW(TAG, "vendorOpen() no bulk OUT endpoint");
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
    // Only the endpoints this API actually transfers on consume a host channel.
    device->endpointChannelCount = static_cast<uint8_t>(device->endpointChannelCount + (foundIn ? 2 : 1));
  }
  else if (device->usbVendorInterfaceNumber != selectedInterface)
  {
    ESP_LOGW(TAG, "vendorOpen() another vendor interface is already open");
    return false;
  }
  else if (device->usbVendorReadOnDemand != (readMode == ESP_USB_HOST_VENDOR_READ_ON_DEMAND))
  {
    // The read mode decides whether an IN transfer is permanently outstanding,
    // which is set up at open time and cannot be changed underneath a running
    // one. Silently keeping the old mode would leave a continuous transfer
    // swallowing the answers vendorReadSync() is waiting for, so this fails
    // instead. Reopen after the device is re-enumerated to change it.
    ESP_LOGW(TAG, "vendorOpen() interface %u is already open in the other read mode",
             selectedInterface);
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }

  device->hasUsbVendorInterface = true;
  device->usbVendorReadOnDemand = readMode == ESP_USB_HOST_VENDOR_READ_ON_DEMAND;
  device->usbVendorInterfaceNumber = selectedInterface;
  device->hasUsbVendorInEndpoint = foundIn;
  device->usbVendorInEndpointAddress = foundIn ? inEndpoint.address : 0;
  device->usbVendorInPacketSize = foundIn ? inEndpoint.maxPacketSize : 0;
  device->hasUsbVendorOutEndpoint = true;
  device->usbVendorOutEndpointAddress = outEndpoint.address;
  device->usbVendorOutPacketSize = outEndpoint.maxPacketSize;

  // On-demand leaves the endpoint idle: no transfer is outstanding until
  // vendorReadSync() submits one.
  EndpointState *existingEndpoint =
      (foundIn && !device->usbVendorReadOnDemand) ? findEndpoint(device->handle, inEndpoint.address) : nullptr;
  if (foundIn && !device->usbVendorReadOnDemand && !existingEndpoint)
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

  if (foundIn)
  {
    ESP_LOGI(TAG, "USB vendor bulk interface ready: address=%u iface=%u in=0x%02x out=0x%02x",
             device->info.address,
             selectedInterface,
             inEndpoint.address,
             outEndpoint.address);
  }
  else
  {
    ESP_LOGI(TAG, "USB vendor bulk interface ready: address=%u iface=%u in=none out=0x%02x",
             device->info.address,
             selectedInterface,
             outEndpoint.address);
  }
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
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "vendorWrite() cannot run from USB client task");
    return false;
  }

  EspUsbHostVendorTransferContext *context = new EspUsbHostVendorTransferContext();
  if (!context)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }
  context->done = xSemaphoreCreateBinary();
  if (!context->done)
  {
    delete context;
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
    vSemaphoreDelete(context->done);
    delete context;
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->usbVendorOutEndpointAddress;
  transfer->callback = vendorTransferCallback;
  transfer->context = context;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(vendor bulk OUT ep=0x%02x) failed: %s",
             device->usbVendorOutEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context->done);
    delete context;
    return false;
  }

  bool done = xSemaphoreTake(context->done, pdMS_TO_TICKS(1000)) == pdTRUE;
  bool callerOwnsTransfer = true;
  if (!done)
  {
    const uint8_t previous = context->state.exchange(ESP_USB_HOST_VENDOR_TRANSFER_ABANDONED,
                                                     std::memory_order_acq_rel);
    if (previous == ESP_USB_HOST_VENDOR_TRANSFER_CALLBACK)
    {
      // The callback won the timeout race and owns no cleanup. Wait for its
      // semaphore give, then keep the normal caller-owned cleanup path.
      xSemaphoreTake(context->done, portMAX_DELAY);
      done = true;
    }
    else
    {
      callerOwnsTransfer = false;
    }
  }
  const bool ok = done && context->status == USB_TRANSFER_STATUS_COMPLETED;
  if (!done)
  {
    ESP_LOGW(TAG, "USB vendor bulk OUT timeout ep=0x%02x", device->usbVendorOutEndpointAddress);
    setLastError(ESP_ERR_TIMEOUT);
    // A submitted transfer remains owned by the HCD until its callback runs.
    // Mark it abandoned before flushing; the eventual callback frees both the
    // transfer and its heap context without blocking this caller indefinitely.
    usb_host_endpoint_halt(device->handle, device->usbVendorOutEndpointAddress);
    usb_host_endpoint_flush(device->handle, device->usbVendorOutEndpointAddress);
    usb_host_endpoint_clear(device->handle, device->usbVendorOutEndpointAddress);
  }
  else if (!ok)
  {
    ESP_LOGW(TAG, "USB vendor bulk OUT failed ep=0x%02x status=%d actual=%u",
             device->usbVendorOutEndpointAddress,
             context->status,
             static_cast<unsigned>(context->actualLength));
    setLastError(ESP_FAIL);
  }

  if (callerOwnsTransfer)
  {
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context->done);
    delete context;
  }

  // A transfer that ends on a packet boundary does not terminate the USB
  // transfer, so protocols that need one get the ZLP here. The length guard also
  // keeps this from recursing on the ZLP itself.
  if (ok && device->usbVendorAutoZlp && length != 0 && device->usbVendorOutPacketSize != 0 &&
      (length % device->usbVendorOutPacketSize) == 0)
  {
    if (vendorWrite(nullptr, 0, address))
    {
      device->usbVendorWriteStats.zlp++;
    }
  }
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

bool EspUsbHost::vendorReadSync(uint8_t *buffer,
                                size_t length,
                                size_t *actualLength,
                                uint32_t timeoutMs,
                                uint8_t address)
{
  if (actualLength)
  {
    *actualLength = 0;
  }

  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "vendorReadSync() called before vendorOpen()");
    return false;
  }
  if (!device->hasUsbVendorInEndpoint)
  {
    ESP_LOGW(TAG, "vendorReadSync() no bulk IN endpoint");
    return false;
  }
  if (!buffer || length == 0)
  {
    ESP_LOGW(TAG, "vendorReadSync() called with no destination");
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "vendorReadSync() cannot run from USB client task");
    return false;
  }

  // An IN transfer length must be a whole number of max-size packets, so the
  // request is rounded up and only what the caller asked for is copied back.
  const uint16_t packetSize = device->usbVendorInPacketSize != 0 ? device->usbVendorInPacketSize : 64;
  const size_t packets = (length + packetSize - 1) / packetSize;
  const size_t requestLength = packets * packetSize;

  EspUsbHostVendorTransferContext *context = new EspUsbHostVendorTransferContext();
  if (!context)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }
  context->done = xSemaphoreCreateBinary();
  if (!context->done)
  {
    delete context;
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(requestLength, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor bulk IN) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context->done);
    delete context;
    return false;
  }

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->usbVendorInEndpointAddress;
  transfer->callback = vendorTransferCallback;
  transfer->context = context;
  transfer->num_bytes = requestLength;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(vendor bulk IN ep=0x%02x) failed: %s",
             device->usbVendorInEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context->done);
    delete context;
    return false;
  }

  bool done = xSemaphoreTake(context->done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  bool callerOwnsTransfer = true;
  if (!done)
  {
    const uint8_t previous = context->state.exchange(ESP_USB_HOST_VENDOR_TRANSFER_ABANDONED,
                                                     std::memory_order_acq_rel);
    if (previous == ESP_USB_HOST_VENDOR_TRANSFER_CALLBACK)
    {
      // The callback won the timeout race and owns no cleanup. Wait for its
      // semaphore give, then keep the normal caller-owned cleanup path.
      xSemaphoreTake(context->done, portMAX_DELAY);
      done = true;
    }
    else
    {
      callerOwnsTransfer = false;
    }
  }

  const bool ok = done && context->status == USB_TRANSFER_STATUS_COMPLETED;
  if (ok)
  {
    const size_t copied = context->actualLength < length ? context->actualLength : length;
    if (copied != 0)
    {
      memcpy(buffer, transfer->data_buffer, copied);
    }
    if (actualLength)
    {
      *actualLength = copied;
    }
  }
  else if (!done)
  {
    ESP_LOGD(TAG, "USB vendor bulk IN timeout ep=0x%02x", device->usbVendorInEndpointAddress);
    setLastError(ESP_ERR_TIMEOUT);
    // A submitted transfer stays owned by the HCD until its callback runs, so it
    // is abandoned rather than freed here; the callback cleans both up.
    usb_host_endpoint_halt(device->handle, device->usbVendorInEndpointAddress);
    usb_host_endpoint_flush(device->handle, device->usbVendorInEndpointAddress);
    usb_host_endpoint_clear(device->handle, device->usbVendorInEndpointAddress);
  }
  else
  {
    ESP_LOGD(TAG, "USB vendor bulk IN failed ep=0x%02x status=%d",
             device->usbVendorInEndpointAddress,
             context->status);
    setLastError(ESP_FAIL);
    // A stalled pipe stays halted, and ESP-IDF then refuses every later submit
    // with ESP_ERR_INVALID_STATE. Clear it here so one failed read does not make
    // the endpoint useless for the rest of the session.
    if (context->status != USB_TRANSFER_STATUS_CANCELED)
    {
      const esp_err_t haltErr = usb_host_endpoint_halt(device->handle, device->usbVendorInEndpointAddress);
      const esp_err_t flushErr = usb_host_endpoint_flush(device->handle, device->usbVendorInEndpointAddress);
      const esp_err_t clearErr = usb_host_endpoint_clear(device->handle, device->usbVendorInEndpointAddress);
      ESP_LOGD(TAG, "vendor bulk IN recovery halt=%s flush=%s clear=%s",
               esp_err_to_name(haltErr), esp_err_to_name(flushErr), esp_err_to_name(clearErr));
    }
  }

  if (callerOwnsTransfer)
  {
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context->done);
    delete context;
  }
  return ok;
}

namespace
{
constexpr uint8_t VENDOR_OUT_SLOT_FREE = 0;
constexpr uint8_t VENDOR_OUT_SLOT_ACQUIRED = 1;
constexpr uint8_t VENDOR_OUT_SLOT_INFLIGHT = 2;
// Bounded wait used when an automatic ZLP needs a slot of its own. The ZLP must
// follow its data transfer, so this waits instead of failing immediately.
constexpr uint32_t VENDOR_OUT_ZLP_WAIT_MS = 100;
} // namespace

int EspUsbHost::vendorOutSlotOf(const DeviceState &device, const uint8_t *buffer) const
{
  if (!buffer)
  {
    return -1;
  }
  for (uint8_t i = 0; i < device.usbVendorOutQueueDepth; i++)
  {
    const usb_transfer_t *transfer = device.usbVendorOutTransfers[i];
    if (transfer && transfer->data_buffer == buffer)
    {
      return i;
    }
  }
  return -1;
}

int EspUsbHost::vendorOutSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const
{
  for (uint8_t i = 0; i < device.usbVendorOutQueueDepth; i++)
  {
    if (device.usbVendorOutTransfers[i] == transfer)
    {
      return i;
    }
  }
  return -1;
}

bool EspUsbHost::vendorWriteQueueBegin(size_t depth, size_t bufferBytes, uint8_t address)
{
  if (depth == 0 || depth > ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH || bufferBytes == 0)
  {
    ESP_LOGW(TAG, "vendorWriteQueueBegin() invalid depth=%u bufferBytes=%u",
             static_cast<unsigned>(depth),
             static_cast<unsigned>(bufferBytes));
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "vendorWriteQueueBegin() called before vendorOpen()");
    return false;
  }
  if (!device->hasUsbVendorOutEndpoint)
  {
    ESP_LOGW(TAG, "vendorWriteQueueBegin() no bulk OUT endpoint");
    return false;
  }

  if (device->usbVendorOutQueueActive)
  {
    // Re-begin with the same shape is a no-op; changing the shape requires an
    // explicit end so in-flight transfers are drained first.
    if (device->usbVendorOutQueueDepth == depth && device->usbVendorOutBufferBytes == bufferBytes)
    {
      return true;
    }
    ESP_LOGW(TAG, "vendorWriteQueueBegin() already active with depth=%u bufferBytes=%u",
             static_cast<unsigned>(device->usbVendorOutQueueDepth),
             static_cast<unsigned>(device->usbVendorOutBufferBytes));
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }

  device->usbVendorOutFreeSlots = xSemaphoreCreateCounting(depth, depth);
  if (!device->usbVendorOutFreeSlots)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  for (size_t i = 0; i < depth; i++)
  {
    usb_transfer_t *transfer = nullptr;
    const esp_err_t err = usb_host_transfer_alloc(bufferBytes, 0, &transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_alloc(vendor bulk OUT queue) failed: %s", esp_err_to_name(err));
      setLastError(err);
      for (size_t j = 0; j < i; j++)
      {
        usb_host_transfer_free(device->usbVendorOutTransfers[j]);
        device->usbVendorOutTransfers[j] = nullptr;
      }
      vSemaphoreDelete(device->usbVendorOutFreeSlots);
      device->usbVendorOutFreeSlots = nullptr;
      return false;
    }
    transfer->device_handle = device->handle;
    transfer->bEndpointAddress = device->usbVendorOutEndpointAddress;
    transfer->callback = vendorOutTransferCallback;
    transfer->context = this;
    device->usbVendorOutTransfers[i] = transfer;
    device->usbVendorOutSlotState[i] = VENDOR_OUT_SLOT_FREE;
  }

  device->usbVendorOutQueueDepth = static_cast<uint8_t>(depth);
  device->usbVendorOutBufferBytes = bufferBytes;
  device->usbVendorOutHalted = false;
  device->usbVendorWriteStats = EspUsbHostVendorWriteStats();
  device->usbVendorOutQueueActive = true;

  ESP_LOGI(TAG, "USB vendor bulk OUT queue ready: address=%u ep=0x%02x depth=%u buffer=%u",
           device->info.address,
           device->usbVendorOutEndpointAddress,
           static_cast<unsigned>(depth),
           static_cast<unsigned>(bufferBytes));
  return true;
}

void EspUsbHost::vendorWriteQueueEnd(uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->usbVendorOutQueueActive)
  {
    return;
  }
  // Stop accepting new work before draining so pending() can reach zero.
  device->usbVendorOutQueueActive = false;
  vendorDrainOut(*device);
}

// Wait for in-flight transfers to complete, then free the pool. Freeing a
// transfer the HCD still owns is a use-after-free in the driver, so a wedged
// transfer intentionally leaks its slot instead (same tradeoff as
// networkDrainTx()).
void EspUsbHost::vendorDrainOut(DeviceState &device)
{
  if (xTaskGetCurrentTaskHandle() != clientTaskHandle_)
  {
    const uint32_t deadline = millis() + 2000;
    while (millis() < deadline)
    {
      bool inFlight = false;
      portENTER_CRITICAL(&vendorOutMux_);
      for (uint8_t i = 0; i < device.usbVendorOutQueueDepth; i++)
      {
        if (device.usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_INFLIGHT)
        {
          inFlight = true;
          break;
        }
      }
      portEXIT_CRITICAL(&vendorOutMux_);
      if (!inFlight)
      {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  releaseVendorOutQueue(device);
}

void EspUsbHost::releaseVendorOutQueue(DeviceState &device)
{
  device.usbVendorOutQueueActive = false;
  for (uint8_t i = 0; i < device.usbVendorOutQueueDepth; i++)
  {
    usb_transfer_t *transfer = device.usbVendorOutTransfers[i];
    const bool inFlight = device.usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_INFLIGHT;
    device.usbVendorOutTransfers[i] = nullptr;
    device.usbVendorOutSlotState[i] = VENDOR_OUT_SLOT_FREE;
    if (!transfer)
    {
      continue;
    }
    if (inFlight)
    {
      ESP_LOGW(TAG, "vendor bulk OUT slot %u still in flight; leaking it to avoid a use-after-free",
               static_cast<unsigned>(i));
      continue;
    }
    usb_host_transfer_free(transfer);
  }
  device.usbVendorOutQueueDepth = 0;
  device.usbVendorOutBufferBytes = 0;
  device.usbVendorOutHalted = false;
  if (device.usbVendorOutFreeSlots)
  {
    vSemaphoreDelete(device.usbVendorOutFreeSlots);
    device.usbVendorOutFreeSlots = nullptr;
  }
}

bool EspUsbHost::vendorWriteQueueReady(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  return device && device->usbVendorOutQueueActive;
}

uint8_t *EspUsbHost::vendorWriteAcquire(size_t *capacity, uint32_t timeoutMs, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->usbVendorOutQueueActive || !device->usbVendorOutFreeSlots)
  {
    ESP_LOGW(TAG, "vendorWriteAcquire() called before vendorWriteQueueBegin()");
    return nullptr;
  }

  if (xSemaphoreTake(device->usbVendorOutFreeSlots, 0) != pdTRUE)
  {
    device->usbVendorWriteStats.queueFullEvents++;
    if (timeoutMs == 0 ||
        xSemaphoreTake(device->usbVendorOutFreeSlots, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
    {
      setLastError(ESP_ERR_TIMEOUT);
      return nullptr;
    }
  }

  uint8_t *buffer = nullptr;
  portENTER_CRITICAL(&vendorOutMux_);
  for (uint8_t i = 0; i < device->usbVendorOutQueueDepth; i++)
  {
    if (device->usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_FREE && device->usbVendorOutTransfers[i])
    {
      device->usbVendorOutSlotState[i] = VENDOR_OUT_SLOT_ACQUIRED;
      buffer = device->usbVendorOutTransfers[i]->data_buffer;
      break;
    }
  }
  portEXIT_CRITICAL(&vendorOutMux_);

  if (!buffer)
  {
    // The semaphore count and the slot states disagree, which should not happen.
    xSemaphoreGive(device->usbVendorOutFreeSlots);
    setLastError(ESP_FAIL);
    return nullptr;
  }

  if (capacity)
  {
    *capacity = device->usbVendorOutBufferBytes;
  }
  return buffer;
}

void EspUsbHost::vendorWriteRelease(uint8_t *buffer, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->usbVendorOutQueueActive)
  {
    return;
  }
  const int slot = vendorOutSlotOf(*device, buffer);
  if (slot < 0)
  {
    return;
  }

  bool released = false;
  portENTER_CRITICAL(&vendorOutMux_);
  if (device->usbVendorOutSlotState[slot] == VENDOR_OUT_SLOT_ACQUIRED)
  {
    device->usbVendorOutSlotState[slot] = VENDOR_OUT_SLOT_FREE;
    released = true;
  }
  portEXIT_CRITICAL(&vendorOutMux_);

  if (released && device->usbVendorOutFreeSlots)
  {
    xSemaphoreGive(device->usbVendorOutFreeSlots);
  }
}

bool EspUsbHost::submitVendorOutSlot(DeviceState &device, int slot, size_t length)
{
  usb_transfer_t *transfer = device.usbVendorOutTransfers[slot];
  if (!transfer)
  {
    return false;
  }

  // A previous transfer error halts the pipe; ESP-IDF then refuses every submit
  // until the halt is cleared. Clearing can block, so it happens here on the
  // caller task rather than in the completion callback.
  if (device.usbVendorOutHalted)
  {
    if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
    {
      ESP_LOGW(TAG, "vendor bulk OUT halted; cannot recover from the USB client task");
      setLastError(ESP_ERR_INVALID_STATE);
      return false;
    }
    usb_host_endpoint_clear(device.handle, device.usbVendorOutEndpointAddress);
    device.usbVendorOutHalted = false;
  }

  transfer->num_bytes = static_cast<int>(length);

  portENTER_CRITICAL(&vendorOutMux_);
  device.usbVendorOutSlotState[slot] = VENDOR_OUT_SLOT_INFLIGHT;
  portEXIT_CRITICAL(&vendorOutMux_);

  const esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(vendor bulk OUT ep=0x%02x len=%u) failed: %s",
             device.usbVendorOutEndpointAddress,
             static_cast<unsigned>(length),
             esp_err_to_name(err));
    setLastError(err);
    portENTER_CRITICAL(&vendorOutMux_);
    device.usbVendorOutSlotState[slot] = VENDOR_OUT_SLOT_FREE;
    portEXIT_CRITICAL(&vendorOutMux_);
    if (device.usbVendorOutFreeSlots)
    {
      xSemaphoreGive(device.usbVendorOutFreeSlots);
    }
    return false;
  }

  device.usbVendorWriteStats.submitted++;
  if (length == 0)
  {
    device.usbVendorWriteStats.zlp++;
  }
  return true;
}

bool EspUsbHost::submitVendorOutZlp(DeviceState &device)
{
  size_t capacity = 0;
  uint8_t *buffer = vendorWriteAcquire(&capacity, VENDOR_OUT_ZLP_WAIT_MS, device.info.address);
  if (!buffer)
  {
    ESP_LOGW(TAG, "no vendor bulk OUT slot for the automatic ZLP; increase the queue depth");
    return false;
  }
  const int slot = vendorOutSlotOf(device, buffer);
  if (slot < 0)
  {
    vendorWriteRelease(buffer, device.info.address);
    return false;
  }
  return submitVendorOutSlot(device, slot, 0);
}

bool EspUsbHost::vendorWriteSubmit(uint8_t *buffer, size_t length, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->usbVendorOutQueueActive)
  {
    ESP_LOGW(TAG, "vendorWriteSubmit() called before vendorWriteQueueBegin()");
    return false;
  }
  if (length > device->usbVendorOutBufferBytes)
  {
    ESP_LOGW(TAG, "vendorWriteSubmit() length=%u exceeds the slot buffer size %u",
             static_cast<unsigned>(length),
             static_cast<unsigned>(device->usbVendorOutBufferBytes));
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }
  const int slot = vendorOutSlotOf(*device, buffer);
  if (slot < 0 || device->usbVendorOutSlotState[slot] != VENDOR_OUT_SLOT_ACQUIRED)
  {
    ESP_LOGW(TAG, "vendorWriteSubmit() buffer was not acquired from this queue");
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  if (!submitVendorOutSlot(*device, slot, length))
  {
    return false;
  }

  if (device->usbVendorAutoZlp && length != 0 && device->usbVendorOutPacketSize != 0 &&
      (length % device->usbVendorOutPacketSize) == 0)
  {
    submitVendorOutZlp(*device);
  }
  return true;
}

bool EspUsbHost::vendorWriteAsync(const uint8_t *data, size_t length, uint8_t address)
{
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "vendorWriteAsync() called with null data");
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  size_t capacity = 0;
  uint8_t *buffer = vendorWriteAcquire(&capacity, 0, address);
  if (!buffer)
  {
    return false;
  }
  if (length > capacity)
  {
    ESP_LOGW(TAG, "vendorWriteAsync() length=%u exceeds the slot buffer size %u",
             static_cast<unsigned>(length),
             static_cast<unsigned>(capacity));
    setLastError(ESP_ERR_INVALID_SIZE);
    vendorWriteRelease(buffer, address);
    return false;
  }
  if (length > 0)
  {
    memcpy(buffer, data, length);
  }
  return vendorWriteSubmit(buffer, length, address);
}

size_t EspUsbHost::vendorWritePending(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    return 0;
  }
  size_t pending = 0;
  for (uint8_t i = 0; i < device->usbVendorOutQueueDepth; i++)
  {
    if (device->usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_INFLIGHT)
    {
      pending++;
    }
  }
  return pending;
}

size_t EspUsbHost::vendorWriteQueueFree(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    return 0;
  }
  size_t free = 0;
  for (uint8_t i = 0; i < device->usbVendorOutQueueDepth; i++)
  {
    if (device->usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_FREE)
    {
      free++;
    }
  }
  return free;
}

bool EspUsbHost::vendorWriteFlush(uint32_t timeoutMs, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    // Completion callbacks run on this task, so waiting here can never progress.
    ESP_LOGW(TAG, "vendorWriteFlush() cannot run from the USB client task");
    return false;
  }

  const uint32_t deadline = millis() + timeoutMs;
  while (vendorWritePending(address) != 0)
  {
    if (millis() >= deadline)
    {
      setLastError(ESP_ERR_TIMEOUT);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

EspUsbHostVendorWriteStats EspUsbHost::vendorWriteStats(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    return EspUsbHostVendorWriteStats();
  }
  // The USB client task updates these counters concurrently. Re-read until two
  // consecutive snapshots agree so a 64-bit byte count cannot be torn.
  EspUsbHostVendorWriteStats stats = device->usbVendorWriteStats;
  for (int i = 0; i < 4; i++)
  {
    const EspUsbHostVendorWriteStats again = device->usbVendorWriteStats;
    if (again.bytes == stats.bytes && again.completed == stats.completed)
    {
      break;
    }
    stats = again;
  }
  return stats;
}

void EspUsbHost::vendorWriteStatsReset(uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (device)
  {
    device->usbVendorWriteStats = EspUsbHostVendorWriteStats();
  }
}

bool EspUsbHost::vendorWriteZlp(uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->hasUsbVendorOutEndpoint)
  {
    return false;
  }
  if (device->usbVendorOutQueueActive)
  {
    return submitVendorOutZlp(*device);
  }
  return vendorWrite(nullptr, 0, address);
}

void EspUsbHost::vendorSetAutoZlp(bool enable, uint8_t address)
{
  DeviceState *device = findUsbVendorDevice(address);
  if (device)
  {
    device->usbVendorAutoZlp = enable;
  }
}

bool EspUsbHost::vendorAutoZlp(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  return device && device->usbVendorAutoZlp;
}

void EspUsbHost::vendorOutTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (!host)
  {
    usb_host_transfer_free(transfer);
    return;
  }

  // Pool membership, not the device handle, decides ownership: on disconnect the
  // device slot can already be reset by the time these canceled transfers are
  // dispatched.
  DeviceState *device = nullptr;
  int slot = -1;
  for (DeviceState &candidate : host->devices_)
  {
    const int found = host->vendorOutSlotOfTransfer(candidate, transfer);
    if (found >= 0)
    {
      device = &candidate;
      slot = found;
      break;
    }
  }
  if (!device)
  {
    // The pool was released while this transfer was in flight. releaseVendorOutQueue()
    // deliberately leaked the transfer, so free it now that the driver is done.
    usb_host_transfer_free(transfer);
    return;
  }

  EspUsbHostVendorWriteStats &stats = device->usbVendorWriteStats;
  stats.completed++;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    stats.bytes += static_cast<uint64_t>(transfer->actual_num_bytes);
  }
  else
  {
    stats.errors++;
    ESP_LOGD(TAG, "vendor bulk OUT status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    host->setLastError(ESP_FAIL);
    if (transfer->status != USB_TRANSFER_STATUS_CANCELED)
    {
      device->usbVendorOutHalted = true;
    }
  }

  portENTER_CRITICAL(&host->vendorOutMux_);
  device->usbVendorOutSlotState[slot] = VENDOR_OUT_SLOT_FREE;
  portEXIT_CRITICAL(&host->vendorOutMux_);
  if (device->usbVendorOutFreeSlots)
  {
    xSemaphoreGive(device->usbVendorOutFreeSlots);
  }
}

uint16_t EspUsbHost::vendorOutPacketSize(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->hasUsbVendorOutEndpoint)
  {
    return 0;
  }
  return device->usbVendorOutPacketSize;
}

uint16_t EspUsbHost::vendorInPacketSize(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->hasUsbVendorInEndpoint)
  {
    return 0;
  }
  return device->usbVendorInPacketSize;
}

uint8_t EspUsbHost::vendorOutEndpoint(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->hasUsbVendorOutEndpoint)
  {
    return 0;
  }
  return device->usbVendorOutEndpointAddress;
}

uint8_t EspUsbHost::vendorInEndpoint(uint8_t address) const
{
  const DeviceState *device = findUsbVendorDevice(address);
  if (!device || !device->hasUsbVendorInEndpoint)
  {
    return 0;
  }
  return device->usbVendorInEndpointAddress;
}

bool EspUsbHost::vendorControlTransfer(uint8_t requestType,
                                      uint8_t request,
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
  // EP0 belongs to the device, not to the vendor interface, so a control request
  // is allowed before vendorOpen(). Prefer the vendor device so that
  // ESP_USB_HOST_ANY_ADDRESS keeps picking the interface the caller opened.
  DeviceState *device = findUsbVendorDevice(address);
  if (!device)
  {
    device = findDevice(address);
  }
  if (!device || !device->handle)
  {
    return false;
  }
  return submitVendorControl(*device, requestType, request, value, index, data, length, actualLength, timeoutMs);
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
  return vendorControlTransfer(VENDOR_IN_REQUEST_TYPE, request, value, index, data, length, actualLength, address, timeoutMs);
}

bool EspUsbHost::vendorControlOut(uint8_t request,
                                  uint16_t value,
                                  uint16_t index,
                                  const uint8_t *data,
                                  size_t length,
                                  uint8_t address,
                                  uint32_t timeoutMs)
{
  return vendorControlTransfer(VENDOR_OUT_REQUEST_TYPE,
                               request,
                               value,
                               index,
                               const_cast<uint8_t *>(data),
                               length,
                               nullptr,
                               address,
                               timeoutMs);
}

namespace
{
constexpr uint8_t SERIAL_OUT_SLOT_FREE = 0;
constexpr uint8_t SERIAL_OUT_SLOT_ACQUIRED = 1;
constexpr uint8_t SERIAL_OUT_SLOT_INFLIGHT = 2;
} // namespace

int EspUsbHost::serialOutSlotOf(const DeviceState &device, const uint8_t *buffer) const
{
  if (!buffer)
  {
    return -1;
  }
  for (uint8_t i = 0; i < device.serialOutQueueDepth; i++)
  {
    const usb_transfer_t *transfer = device.serialOutTransfers[i];
    if (transfer && transfer->data_buffer == buffer)
    {
      return i;
    }
  }
  return -1;
}

int EspUsbHost::serialOutSlotOfTransfer(const DeviceState &device, const usb_transfer_t *transfer) const
{
  for (uint8_t i = 0; i < device.serialOutQueueDepth; i++)
  {
    if (device.serialOutTransfers[i] == transfer)
    {
      return i;
    }
  }
  return -1;
}

bool EspUsbHost::serialWriteQueueBegin(size_t depth, size_t bufferBytes, uint8_t address)
{
  if (depth == 0 || depth > ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH || bufferBytes == 0)
  {
    ESP_LOGW(TAG, "serialWriteQueueBegin() invalid depth=%u bufferBytes=%u",
             static_cast<unsigned>(depth),
             static_cast<unsigned>(bufferBytes));
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "serialWriteQueueBegin() called before a CDC OUT endpoint is ready");
    return false;
  }

  if (device->serialOutQueueActive)
  {
    // Re-begin with the same shape is a no-op; changing the shape requires an
    // explicit end so in-flight transfers are drained first.
    if (device->serialOutQueueDepth == depth && device->serialOutBufferBytes == bufferBytes)
    {
      return true;
    }
    ESP_LOGW(TAG, "serialWriteQueueBegin() already active with depth=%u bufferBytes=%u",
             static_cast<unsigned>(device->serialOutQueueDepth),
             static_cast<unsigned>(device->serialOutBufferBytes));
    setLastError(ESP_ERR_INVALID_STATE);
    return false;
  }

  device->serialOutFreeSlots = xSemaphoreCreateCounting(depth, depth);
  if (!device->serialOutFreeSlots)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  for (size_t i = 0; i < depth; i++)
  {
    usb_transfer_t *transfer = nullptr;
    const esp_err_t err = usb_host_transfer_alloc(bufferBytes, 0, &transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_alloc(serial OUT queue) failed: %s", esp_err_to_name(err));
      setLastError(err);
      for (size_t j = 0; j < i; j++)
      {
        usb_host_transfer_free(device->serialOutTransfers[j]);
        device->serialOutTransfers[j] = nullptr;
      }
      vSemaphoreDelete(device->serialOutFreeSlots);
      device->serialOutFreeSlots = nullptr;
      return false;
    }
    transfer->device_handle = device->handle;
    transfer->bEndpointAddress = device->serialOutEndpointAddress;
    transfer->callback = serialOutTransferCallback;
    transfer->context = this;
    device->serialOutTransfers[i] = transfer;
    device->serialOutSlotState[i] = SERIAL_OUT_SLOT_FREE;
  }

  device->serialOutQueueDepth = static_cast<uint8_t>(depth);
  device->serialOutBufferBytes = bufferBytes;
  device->serialOutHalted = false;
  device->serialWriteStats = EspUsbHostSerialWriteStats();
  device->serialOutQueueActive = true;

  ESP_LOGI(TAG, "CDC serial OUT queue ready: address=%u ep=0x%02x depth=%u buffer=%u",
           device->info.address,
           device->serialOutEndpointAddress,
           static_cast<unsigned>(depth),
           static_cast<unsigned>(bufferBytes));
  return true;
}

void EspUsbHost::serialWriteQueueEnd(uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device || !device->serialOutQueueActive)
  {
    return;
  }
  // Stop accepting new work before draining so pending() can reach zero.
  device->serialOutQueueActive = false;
  serialDrainOut(*device);
}

// Wait for in-flight transfers to complete, then free the pool. Freeing a
// transfer the HCD still owns is a use-after-free in the driver, so a wedged
// transfer intentionally leaks its slot instead (same tradeoff as
// vendorDrainOut()).
void EspUsbHost::serialDrainOut(DeviceState &device)
{
  if (xTaskGetCurrentTaskHandle() != clientTaskHandle_)
  {
    const uint32_t deadline = millis() + 2000;
    while (millis() < deadline)
    {
      bool inFlight = false;
      portENTER_CRITICAL(&serialOutMux_);
      for (uint8_t i = 0; i < device.serialOutQueueDepth; i++)
      {
        if (device.serialOutSlotState[i] == SERIAL_OUT_SLOT_INFLIGHT)
        {
          inFlight = true;
          break;
        }
      }
      portEXIT_CRITICAL(&serialOutMux_);
      if (!inFlight)
      {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  releaseSerialOutQueue(device);
}

void EspUsbHost::releaseSerialOutQueue(DeviceState &device)
{
  device.serialOutQueueActive = false;
  for (uint8_t i = 0; i < device.serialOutQueueDepth; i++)
  {
    usb_transfer_t *transfer = device.serialOutTransfers[i];
    const bool inFlight = device.serialOutSlotState[i] == SERIAL_OUT_SLOT_INFLIGHT;
    device.serialOutTransfers[i] = nullptr;
    device.serialOutSlotState[i] = SERIAL_OUT_SLOT_FREE;
    if (!transfer)
    {
      continue;
    }
    if (inFlight)
    {
      ESP_LOGW(TAG, "serial OUT slot %u still in flight; leaking it to avoid a use-after-free",
               static_cast<unsigned>(i));
      continue;
    }
    usb_host_transfer_free(transfer);
  }
  device.serialOutQueueDepth = 0;
  device.serialOutBufferBytes = 0;
  device.serialOutHalted = false;
  if (device.serialOutFreeSlots)
  {
    vSemaphoreDelete(device.serialOutFreeSlots);
    device.serialOutFreeSlots = nullptr;
  }
}

bool EspUsbHost::serialWriteQueueReady(uint8_t address) const
{
  const DeviceState *device = findSerialDevice(address);
  return device && device->serialOutQueueActive;
}

uint8_t *EspUsbHost::serialWriteAcquire(size_t *capacity, uint32_t timeoutMs, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device || !device->serialOutQueueActive || !device->serialOutFreeSlots)
  {
    ESP_LOGW(TAG, "serialWriteAcquire() called before serialWriteQueueBegin()");
    return nullptr;
  }

  if (xSemaphoreTake(device->serialOutFreeSlots, 0) != pdTRUE)
  {
    device->serialWriteStats.queueFullEvents++;
    if (timeoutMs == 0 ||
        xSemaphoreTake(device->serialOutFreeSlots, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
    {
      setLastError(ESP_ERR_TIMEOUT);
      return nullptr;
    }
  }

  uint8_t *buffer = nullptr;
  portENTER_CRITICAL(&serialOutMux_);
  for (uint8_t i = 0; i < device->serialOutQueueDepth; i++)
  {
    if (device->serialOutSlotState[i] == SERIAL_OUT_SLOT_FREE && device->serialOutTransfers[i])
    {
      device->serialOutSlotState[i] = SERIAL_OUT_SLOT_ACQUIRED;
      buffer = device->serialOutTransfers[i]->data_buffer;
      break;
    }
  }
  portEXIT_CRITICAL(&serialOutMux_);

  if (!buffer)
  {
    // The semaphore count and the slot states disagree, which should not happen.
    xSemaphoreGive(device->serialOutFreeSlots);
    setLastError(ESP_FAIL);
    return nullptr;
  }

  if (capacity)
  {
    *capacity = device->serialOutBufferBytes;
  }
  return buffer;
}

void EspUsbHost::serialWriteRelease(uint8_t *buffer, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device || !device->serialOutQueueActive)
  {
    return;
  }
  const int slot = serialOutSlotOf(*device, buffer);
  if (slot < 0)
  {
    return;
  }

  bool released = false;
  portENTER_CRITICAL(&serialOutMux_);
  if (device->serialOutSlotState[slot] == SERIAL_OUT_SLOT_ACQUIRED)
  {
    device->serialOutSlotState[slot] = SERIAL_OUT_SLOT_FREE;
    released = true;
  }
  portEXIT_CRITICAL(&serialOutMux_);

  if (released && device->serialOutFreeSlots)
  {
    xSemaphoreGive(device->serialOutFreeSlots);
  }
}

bool EspUsbHost::submitSerialOutSlot(DeviceState &device, int slot, size_t length)
{
  usb_transfer_t *transfer = device.serialOutTransfers[slot];
  if (!transfer)
  {
    return false;
  }

  // A previous transfer error halts the pipe; ESP-IDF then refuses every submit
  // until the halt is cleared. Clearing can block, so it happens here on the
  // caller task rather than in the completion callback.
  if (device.serialOutHalted)
  {
    if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
    {
      ESP_LOGW(TAG, "serial OUT halted; cannot recover from the USB client task");
      setLastError(ESP_ERR_INVALID_STATE);
      return false;
    }
    usb_host_endpoint_clear(device.handle, device.serialOutEndpointAddress);
    device.serialOutHalted = false;
  }

  transfer->num_bytes = static_cast<int>(length);

  portENTER_CRITICAL(&serialOutMux_);
  device.serialOutSlotState[slot] = SERIAL_OUT_SLOT_INFLIGHT;
  portEXIT_CRITICAL(&serialOutMux_);

  const esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(serial OUT ep=0x%02x len=%u) failed: %s",
             device.serialOutEndpointAddress,
             static_cast<unsigned>(length),
             esp_err_to_name(err));
    setLastError(err);
    portENTER_CRITICAL(&serialOutMux_);
    device.serialOutSlotState[slot] = SERIAL_OUT_SLOT_FREE;
    portEXIT_CRITICAL(&serialOutMux_);
    if (device.serialOutFreeSlots)
    {
      xSemaphoreGive(device.serialOutFreeSlots);
    }
    return false;
  }

  device.serialWriteStats.submitted++;
  if (length == 0)
  {
    device.serialWriteStats.zlp++;
  }
  return true;
}

bool EspUsbHost::serialWriteSubmit(uint8_t *buffer, size_t length, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device || !device->serialOutQueueActive)
  {
    ESP_LOGW(TAG, "serialWriteSubmit() called before serialWriteQueueBegin()");
    return false;
  }
  if (length > device->serialOutBufferBytes)
  {
    ESP_LOGW(TAG, "serialWriteSubmit() length=%u exceeds the slot buffer size %u",
             static_cast<unsigned>(length),
             static_cast<unsigned>(device->serialOutBufferBytes));
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }
  const int slot = serialOutSlotOf(*device, buffer);
  if (slot < 0 || device->serialOutSlotState[slot] != SERIAL_OUT_SLOT_ACQUIRED)
  {
    ESP_LOGW(TAG, "serialWriteSubmit() buffer was not acquired from this queue");
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  return submitSerialOutSlot(*device, slot, length);
}

bool EspUsbHost::serialWriteAsync(const uint8_t *data, size_t length, uint32_t timeoutMs, uint8_t address)
{
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "serialWriteAsync() called with null data");
    setLastError(ESP_ERR_INVALID_ARG);
    return false;
  }

  size_t capacity = 0;
  uint8_t *buffer = serialWriteAcquire(&capacity, timeoutMs, address);
  if (!buffer)
  {
    return false;
  }
  if (length > capacity)
  {
    ESP_LOGW(TAG, "serialWriteAsync() length=%u exceeds the slot buffer size %u",
             static_cast<unsigned>(length),
             static_cast<unsigned>(capacity));
    setLastError(ESP_ERR_INVALID_SIZE);
    serialWriteRelease(buffer, address);
    return false;
  }
  if (length > 0)
  {
    memcpy(buffer, data, length);
  }
  return serialWriteSubmit(buffer, length, address);
}

size_t EspUsbHost::serialWritePending(uint8_t address) const
{
  const DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    return 0;
  }
  size_t pending = 0;
  for (uint8_t i = 0; i < device->serialOutQueueDepth; i++)
  {
    if (device->serialOutSlotState[i] == SERIAL_OUT_SLOT_INFLIGHT)
    {
      pending++;
    }
  }
  return pending;
}

size_t EspUsbHost::serialWriteQueueFree(uint8_t address) const
{
  const DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    return 0;
  }
  size_t free = 0;
  for (uint8_t i = 0; i < device->serialOutQueueDepth; i++)
  {
    if (device->serialOutSlotState[i] == SERIAL_OUT_SLOT_FREE)
    {
      free++;
    }
  }
  return free;
}

bool EspUsbHost::serialWriteFlush(uint32_t timeoutMs, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    // Completion callbacks run on this task, so waiting here can never progress.
    ESP_LOGW(TAG, "serialWriteFlush() cannot run from the USB client task");
    return false;
  }

  const uint32_t deadline = millis() + timeoutMs;
  while (serialWritePending(address) != 0)
  {
    if (millis() >= deadline)
    {
      setLastError(ESP_ERR_TIMEOUT);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

EspUsbHostSerialWriteStats EspUsbHost::serialWriteStats(uint8_t address) const
{
  const DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    return EspUsbHostSerialWriteStats();
  }
  // The USB client task updates these counters concurrently. Re-read until two
  // consecutive snapshots agree so a 64-bit byte count cannot be torn.
  EspUsbHostSerialWriteStats stats = device->serialWriteStats;
  for (int i = 0; i < 4; i++)
  {
    const EspUsbHostSerialWriteStats again = device->serialWriteStats;
    if (again.bytes == stats.bytes && again.completed == stats.completed)
    {
      break;
    }
    stats = again;
  }
  return stats;
}

void EspUsbHost::serialWriteStatsReset(uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (device)
  {
    device->serialWriteStats = EspUsbHostSerialWriteStats();
  }
}

uint16_t EspUsbHost::serialOutPacketSize(uint8_t address) const
{
  const DeviceState *device = findSerialDevice(address);
  return device ? device->serialOutPacketSize : 0;
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

  // With the queue active, go through it so the caller inherits its backpressure
  // rather than growing an unbounded set of one-shot transfers. Waiting for a
  // slot only works off the USB client task, where the completions run.
  if (device->serialOutQueueActive && length <= device->serialOutBufferBytes)
  {
    const uint32_t timeoutMs = xTaskGetCurrentTaskHandle() == clientTaskHandle_
                                   ? 0
                                   : ESP_USB_HOST_SERIAL_WRITE_DEFAULT_TIMEOUT_MS;
    return serialWriteAsync(data, length, timeoutMs, device->info.address);
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

bool EspUsbHost::getMidiPortInfo(EspUsbHostMidiPortInfo &info, uint8_t address) const
{
  const DeviceState *device = findMidiDevice(address);
  if (!device)
  {
    return false;
  }
  info = EspUsbHostMidiPortInfo();
  info.address = device->info.address;
  info.interfaceNumber = device->midiInterfaceNumber;
  info.inCableCount = device->midiInCableCount;
  info.outCableCount = device->midiOutCableCount;
  return true;
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
  DeviceState *device = findAudioInputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioInputStart() called before a USB Audio IN endpoint is ready");
    return false;
  }

  const EspUsbHostAudioStreamSelection selection =
      espUsbHostSelectAudioStreamForFormat(device->audioStreamInfos,
                                           device->audioStreamInfoCount,
                                           true,
                                           channels,
                                           bitsPerSample,
                                           sampleRate);
  if (!selection)
  {
    ESP_LOGW(TAG, "No matching USB Audio IN stream: channels=%u bits=%u rate=%lu",
             channels,
             bitsPerSample,
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  return audioInputStart(device->audioStreamInfos[selection.index],
                         selection.sampleRate,
                         device->info.address);
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

  if (!stream.startable)
  {
    ESP_LOGW(TAG, "audioInputStart() called with a format-only stream: iface=%u alt=%u ep=0x%02x",
             stream.interfaceNumber,
             stream.alternate,
             stream.endpointAddress);
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

  bool submitted = applyAudioStreamSampleRate(*device, stream, selectedRate);
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
  if (device->audioProtocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    // UAC2 programs the rate once per Clock Source entity, not once per endpoint.
    uint8_t applied[ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES] = {};
    uint8_t appliedCount = 0;
    for (uint8_t i = 0; i < device->audioStreamInfoCount; i++)
    {
      const uint8_t clockSourceId = device->audioStreamInfos[i].clockSourceId;
      if (device->audioStreamInfos[i].protocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 ||
          clockSourceId == 0)
      {
        continue;
      }
      bool alreadyApplied = false;
      for (uint8_t j = 0; j < appliedCount; j++)
      {
        if (applied[j] == clockSourceId)
        {
          alreadyApplied = true;
          break;
        }
      }
      if (alreadyApplied || appliedCount >= ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES)
      {
        continue;
      }
      applied[appliedCount++] = clockSourceId;
      submitted = submitAudioClockSampleRate(*device, clockSourceId, device->audioSampleRate) && submitted;
    }
    return submitted;
  }
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
  DeviceState *device = findAudioOutputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioOutputStart() called before a USB Audio OUT endpoint is ready");
    return false;
  }

  const EspUsbHostAudioStreamSelection selection =
      espUsbHostSelectAudioStreamForFormat(device->audioStreamInfos,
                                           device->audioStreamInfoCount,
                                           false,
                                           channels,
                                           bitsPerSample,
                                           sampleRate);
  if (!selection)
  {
    ESP_LOGW(TAG, "No matching USB Audio OUT stream: channels=%u bits=%u rate=%lu",
             channels,
             bitsPerSample,
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  return audioOutputStart(device->audioStreamInfos[selection.index],
                          selection.sampleRate,
                          device->info.address);
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
  if (!stream.startable)
  {
    ESP_LOGW(TAG, "audioOutputStart() called with a format-only stream: iface=%u alt=%u ep=0x%02x",
             stream.interfaceNumber,
             stream.alternate,
             stream.endpointAddress);
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

  bool submitted = applyAudioStreamSampleRate(*device, stream, selectedRate);
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

  if (!startAudioFeedback(*device))
  {
    // Playback itself is running; without feedback it just stays at the negotiated
    // rate, which is what a synchronous device does anyway.
    ESP_LOGW(TAG, "USB Audio feedback polling unavailable: ep=0x%02x",
             device->audioOutFeedbackEndpointAddress);
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

bool EspUsbHost::audioOutputHasFeedback(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device && device->audioOutFeedbackTransfer != nullptr;
}

uint32_t EspUsbHost::audioOutputFeedbackRate(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device ? device->audioOutFeedbackRate : 0;
}

uint32_t EspUsbHost::audioOutputFeedbackUpdates(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device ? device->audioOutFeedbackUpdates : 0;
}

uint32_t EspUsbHost::audioOutputFeedbackRejects(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device ? device->audioOutFeedbackRejects : 0;
}

uint32_t EspUsbHost::audioOutputRate(uint8_t address) const
{
  const DeviceState *device = findAudioOutputDevice(address);
  return device ? audioOutputPacingRate(*device) : 0;
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
  if (!audioFeatureControl(*device, audioCurRequest(unit->protocol, true), unit->unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel, &value, sizeof(value), true, timeoutMs))
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
  return audioFeatureControl(*device, audioCurRequest(unit->protocol, false), unit->unitId, USB_AUDIO_FEATURE_MUTE_CONTROL, channel, &value, sizeof(value), false, timeoutMs);
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
  if (!audioFeatureControl(*device, audioCurRequest(unit->protocol, true), unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), true, timeoutMs))
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
  return audioFeatureControl(*device, audioCurRequest(unit->protocol, false), unit->unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel, value, sizeof(value), false, timeoutMs);
}

bool EspUsbHost::audioGetVolumeRange(EspUsbHostAudioVolumeRange &range, uint8_t address, uint8_t unitId, uint8_t channel, uint32_t timeoutMs)
{
  DeviceState *device = findAudioControlDevice(address);
  const EspUsbHostAudioFeatureUnitInfo *unit = device ? findAudioFeatureUnit(*device, unitId, USB_AUDIO_FEATURE_VOLUME_CONTROL, channel) : nullptr;
  if (!unit)
  {
    return false;
  }
  if (unit->protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    // UAC2 replaced GET_MIN/GET_MAX/GET_RES with a single RANGE request that
    // returns wNumSubRanges followed by MIN/MAX/RES triples. Only the first
    // subrange is reported: the volume APIs work with one continuous range.
    uint8_t payload[2 + 6] = {};
    if (!audioFeatureControl(*device, USB_AUDIO_REQUEST_RANGE, unit->unitId,
                             USB_AUDIO_FEATURE_VOLUME_CONTROL, channel,
                             payload, sizeof(payload), true, timeoutMs))
    {
      return false;
    }
    return espUsbHostAudioDecodeVolumeRange(payload, sizeof(payload), range);
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

    if ((endpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0)
    {
      espUsbHostCacheSyncBeforeInTransfer(transfer);
    }
    err = usb_host_transfer_submit(transfer);
    if (err == ESP_ERR_INVALID_STATE)
    {
      // The pipe is still halted from an earlier failed command. Recover it
      // here so one bad command cannot wedge every later MSC transfer.
      ESP_LOGW(TAG, "MSC endpoint 0x%02x is halted, recovering before retry", endpointAddress);
      if (mscClearEndpointHalt(device, endpointAddress, timeoutMs))
      {
        err = usb_host_transfer_submit(transfer);
      }
    }
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
      usb_host_endpoint_halt(device.handle, endpointAddress);
      usb_host_endpoint_flush(device.handle, endpointAddress);
      // The transfer remains owned by the HCD until the flushed URB callback
      // runs. Waiting here prevents a late dequeue from touching freed memory.
      xSemaphoreTake(context.done, portMAX_DELAY);
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
  uint8_t failedDataEndpoint = 0;
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
    bool cswTransferOk = submitAndWait(device.mscInEndpointAddress,
                                       nullptr,
                                       reinterpret_cast<uint8_t *>(&csw),
                                       sizeof(csw),
                                       actual);
    if (!cswTransferOk)
    {
      // Bulk-Only Transport 6.7.2: a stalled status phase is recovered by
      // clearing the bulk-IN halt and reading the CSW once more. Do this even
      // when the full reset recovery is suppressed, otherwise the halted pipe
      // makes every later transfer fail to enqueue.
      if (mscClearEndpointHalt(device, device.mscInEndpointAddress, timeoutMs))
      {
        actual = 0;
        csw = EspUsbHostMscCsw();
        cswTransferOk = submitAndWait(device.mscInEndpointAddress,
                                      nullptr,
                                      reinterpret_cast<uint8_t *>(&csw),
                                      sizeof(csw),
                                      actual);
      }
    }
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
      else
      {
        mscClearEndpointHalt(device, device.mscOutEndpointAddress, timeoutMs);
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
      else
      {
        mscClearEndpointHalt(device, device.mscInEndpointAddress, timeoutMs);
        mscClearEndpointHalt(device, device.mscOutEndpointAddress, timeoutMs);
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
        if (dataLength > 0 && csw.dataResidue > 0)
        {
          failedDataEndpoint = dataIn ? device.mscInEndpointAddress : device.mscOutEndpointAddress;
        }
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
    else
    {
      // Still leave both bulk pipes usable for the next command.
      mscClearEndpointHalt(device, device.mscInEndpointAddress, timeoutMs);
      mscClearEndpointHalt(device, device.mscOutEndpointAddress, timeoutMs);
    }
  }

  vSemaphoreDelete(context.done);
  if (failedDataEndpoint != 0 && !mscClearEndpointHalt(device, failedDataEndpoint, timeoutMs))
  {
    if (allowResetRecovery)
    {
      mscResetRecovery(device, timeoutMs);
    }
  }
  if (requestSenseAfterCommand)
  {
    EspUsbHostMscSense sense;
    mscRequestSense(sense, device.info.address, timeoutMs);
  }
  return ok;
}

bool EspUsbHost::mscClearEndpointHalt(DeviceState &device, uint8_t endpointAddress, uint32_t timeoutMs)
{
  if (!device.handle || endpointAddress == 0)
  {
    return false;
  }

  esp_err_t err = usb_host_endpoint_halt(device.handle, endpointAddress);
  if (err == ESP_OK)
  {
    err = usb_host_endpoint_flush(device.handle, endpointAddress);
  }
  if (err == ESP_OK)
  {
    err = usb_host_endpoint_clear(device.handle, endpointAddress);
  }
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "MSC host endpoint recovery failed ep=0x%02x: %s",
             endpointAddress,
             esp_err_to_name(err));
    setLastError(err);
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
  err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(MSC ClearFeature ep=0x%02x) failed: %s",
             endpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                         USB_BM_REQUEST_TYPE_TYPE_STANDARD |
                         USB_BM_REQUEST_TYPE_RECIP_ENDPOINT;
  setup->bRequest = USB_REQUEST_CLEAR_FEATURE;
  setup->wValue = 0; // ENDPOINT_HALT
  setup->wIndex = endpointAddress;
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
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(MSC ClearFeature ep=0x%02x) failed: %s",
             endpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  const bool ok = done && context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (!done)
  {
    ESP_LOGW(TAG, "MSC ClearFeature timeout ep=0x%02x", endpointAddress);
    setLastError(ESP_ERR_TIMEOUT);
    // A submitted control transfer cannot be freed until its callback returns.
    xSemaphoreTake(context.done, portMAX_DELAY);
  }
  else if (!ok)
  {
    ESP_LOGW(TAG, "MSC ClearFeature failed ep=0x%02x status=%d", endpointAddress, context.status);
    setLastError(ESP_FAIL);
  }
  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
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
    // A submitted control transfer cannot be freed until its callback returns.
    xSemaphoreTake(context.done, portMAX_DELAY);
  }
  else if (!resetOk)
  {
    ESP_LOGW(TAG, "MSC reset failed status=%d", context.status);
    setLastError(ESP_FAIL);
  }
  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);

  bool clearOk = resetOk;
  if (resetOk)
  {
    const bool clearInOk = mscClearEndpointHalt(device, device.mscInEndpointAddress, timeoutMs);
    const bool clearOutOk = mscClearEndpointHalt(device, device.mscOutEndpointAddress, timeoutMs);
    clearOk = clearInOk && clearOutOk;
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

  if (device->mscSyncCacheUnsupported)
  {
    // Already known to fail on this device: reissuing it only stalls the bulk
    // pipes again, so report the failure without touching the bus.
    setLastError(ESP_ERR_NOT_SUPPORTED);
    return false;
  }

  uint8_t command[10] = {};
  command[0] = SCSI_CMD_SYNCHRONIZE_CACHE_10;
  if (mscCommand(*device, command, sizeof(command), nullptr, 0, false, timeoutMs))
  {
    return true;
  }
  ESP_LOGW(TAG, "MSC SYNCHRONIZE CACHE failed on addr=%u, skipping it for this device",
           device->info.address);
  device->mscSyncCacheUnsupported = true;
  return false;
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
  mount->skipSyncCache = skipSyncCache || device->mscSyncCacheUnsupported;
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
    if (mountResult == FR_NO_FILESYSTEM)
    {
      // FatFs derives the FAT type from the cluster count, so a volume
      // formatted as FAT32 with fewer than 65525 clusters (large cluster size
      // on a small medium) is rejected even though PCs mount it. Reformat with
      // a smaller cluster size, or as FAT16/FAT12.
      ESP_LOGW(TAG, "mscMount(%s): no FatFs-compatible volume (FAT12/16/32 only, "
                    "and a FAT32 volume needs more than 65525 clusters)",
               basePath);
    }
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
    // Best effort: the volume is going away either way, and the result is
    // deliberately ignored, so a device that rejects SYNCHRONIZE CACHE must not
    // leave lastError() reporting a failure for an unmount that succeeded.
    // mscSynchronizeCache() logs it and marks the device on its own.
    const esp_err_t errorBeforeSync = lastError_;
    mscSynchronizeCache(mount->address);
    lastError_ = errorBeforeSync;
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

void EspUsbHost::mscUnmountAll()
{
  for (EspUsbHostMscFatMount &mount : mscFatMounts)
  {
    if (!mount.inUse || mount.host != this)
    {
      continue;
    }
    // mscUnmount() clears the entry, so the reference stays valid for the rest
    // of the loop.
    mscUnmount(mount.basePath);
  }
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

  device.audioOutFrameAccumulator += audioOutputPacingRate(device);
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
    out.printf("  Audio Feature Unit iface=%u unit=%u source=%u channels=%u control_size=%u master=0x%lx proto=%s\n",
               unit.interfaceNumber,
               unit.unitId,
               unit.sourceId,
               unit.channelCount,
               unit.controlSize,
               static_cast<unsigned long>(unit.masterControls),
               unit.protocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 ? "UAC2" : "UAC1");
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
  const EspUsbHostFifoConfig &fifo = config_.fifo;
  if (fifo.rxFifoLines != 0 || fifo.nptxFifoLines != 0 || fifo.ptxFifoLines != 0)
  {
#if defined(ESP_USB_HOST_HAS_FIFO_SETTINGS)
    // The driver only accepts the whole split, so a missing RX or NPTX size
    // would leave control transfers with no FIFO at all.
    if (fifo.rxFifoLines == 0 || fifo.nptxFifoLines == 0)
    {
      ESP_LOGE(TAG, "FIFO config needs rxFifoLines and nptxFifoLines > 0");
      setLastError(ESP_ERR_INVALID_ARG);
      running_ = false;
      taskHandle_ = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    const uint32_t totalLines = fifo.rxFifoLines + fifo.nptxFifoLines + fifo.ptxFifoLines;
    const uint32_t capacityLines = hostFifoCapacityLines(config_.port);
    if (totalLines > capacityLines)
    {
      ESP_LOGE(TAG, "FIFO config total %lu lines exceeds the %lu lines this port has",
               (unsigned long)totalLines,
               (unsigned long)capacityLines);
      setLastError(ESP_ERR_INVALID_SIZE);
      running_ = false;
      taskHandle_ = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    hostConfig.fifo_settings_custom.rx_fifo_lines = fifo.rxFifoLines;
    hostConfig.fifo_settings_custom.nptx_fifo_lines = fifo.nptxFifoLines;
    hostConfig.fifo_settings_custom.ptx_fifo_lines = fifo.ptxFifoLines;
    // The MPS limits are logged as well: they are what an endpoint claim is
    // checked against, so a claim failing with ESP_ERR_NOT_SUPPORTED can be
    // compared against this line directly.
    ESP_LOGI(TAG,
             "FIFO lines rx=%lu nptx=%lu ptx=%lu (total=%lu) -> max MPS in=%lu bulk_out=%lu periodic_out=%lu",
             (unsigned long)fifo.rxFifoLines,
             (unsigned long)fifo.nptxFifoLines,
             (unsigned long)fifo.ptxFifoLines,
             (unsigned long)totalLines,
             (unsigned long)((fifo.rxFifoLines - 2) * 4),
             (unsigned long)(fifo.nptxFifoLines * 4),
             (unsigned long)(fifo.ptxFifoLines * 4));
#else
    ESP_LOGW(TAG, "FIFO config needs arduino-esp32 3.3.0 or newer; using the driver default");
#endif
  }

#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  if (enumerationHost_ && enumerationHost_ != this)
  {
    ESP_LOGE(TAG, "Another EspUsbHost instance owns the enumeration callback");
    setLastError(ESP_ERR_INVALID_STATE);
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  enumerationHost_ = this;
  hostConfig.enum_filter_cb = enumerationFilterCallback;
#endif
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  hostConfig.peripheral_map = hostPeripheralMap(config_.port);
  // Keep the root port stopped until HCFG.FSLSSUPP has been applied. Without
  // this, an already attached hub can begin HS negotiation inside
  // usb_host_install(), racing the register write below.
  hostConfig.root_port_unpowered = config_.experimentalForceFullSpeed;
#endif

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_install() failed: %s", esp_err_to_name(err));
    setLastError(err);
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    if (enumerationHost_ == this)
    {
      enumerationHost_ = nullptr;
    }
#endif
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (config_.experimentalForceFullSpeed)
  {
    usb_dwc_ll_hcfg_set_fsls_supp_only(&USB_DWC_HS);
    ESP_LOGI(TAG,
             "Experimental P4 HS-port full-speed-only mode: HCFG=0x%08lx FSLSSUPP=%u",
             static_cast<unsigned long>(USB_DWC_HS.hcfg_reg.val),
             static_cast<unsigned>(USB_DWC_HS.hcfg_reg.fslssupp));
  }
#endif

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
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    if (enumerationHost_ == this)
    {
      enumerationHost_ = nullptr;
    }
#endif
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (config_.experimentalForceFullSpeed)
  {
    err = usb_host_lib_set_root_port_power(true);
    if (err != ESP_OK)
    {
      const esp_err_t powerError = err;
      ESP_LOGE(TAG, "usb_host_lib_set_root_port_power() failed: %s", esp_err_to_name(err));
      running_ = false;
      releaseClientResources();
      uninstallHostLibrary(1000);
      setLastError(powerError);
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
      if (enumerationHost_ == this)
      {
        enumerationHost_ = nullptr;
      }
#endif
      taskHandle_ = nullptr;
      vTaskDelete(nullptr);
      return;
    }
  }
#endif

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
    releaseClientResources();
    uninstallHostLibrary(1000);
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    if (enumerationHost_ == this)
    {
      enumerationHost_ = nullptr;
    }
#endif
    ready_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
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

  const uint32_t clientStopStartedAtMs = millis();
  while (clientTaskHandle_ && millis() - clientStopStartedAtMs < 2500)
  {
    delay(1);
  }
  if (clientTaskHandle_ || clientHandle_)
  {
    ESP_LOGW(TAG, "USB Host client did not finish shutdown");
    setLastError(ESP_ERR_TIMEOUT);
  }
  else if (!uninstallHostLibrary(2000))
  {
    ESP_LOGW(TAG, "USB Host Library uninstall did not complete");
  }

  ready_ = false;
#if defined(CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK) && CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
  if (enumerationHost_ == this)
  {
    enumerationHost_ = nullptr;
  }
#endif
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
      if (device.inUse && device.disconnectPending)
      {
        finalizeDisconnectedDevice(device);
      }
    }
    for (DeviceState &device : devices_)
    {
      if (!device.inUse || !deviceHasKeyboard(device) || !device.keyboardLedDirty || device.keyboardLedPending)
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
      if (!ep.inUse || !ep.recoveryPending)
      {
        continue;
      }
      ep.recoveryPending = false;
      DeviceState *dev = findDeviceByHandle(ep.deviceHandle);
      if (!dev || !ep.transfer || !ep.deviceHandle)
      {
        continue;
      }

      // Endpoint callbacks are dispatched before DEV_GONE events. Defer error
      // recovery until usb_host_client_handle_events() has processed both, so a
      // disconnected device is released instead of receiving a new URB.
      esp_err_t clearErr = usb_host_endpoint_clear(ep.deviceHandle, ep.address);
      if (clearErr != ESP_OK)
      {
        ESP_LOGD(TAG, "usb_host_endpoint_clear(recovery ep=0x%02x) failed: %s",
                 ep.address,
                 esp_err_to_name(clearErr));
        setLastError(clearErr);
        continue;
      }

      uint8_t ledInterfaceNumber = 0;
      uint8_t ledReportId = 0;
      const bool isKeyboardEndpoint = keyboardLedTarget(*dev, ledInterfaceNumber, ledReportId) &&
                                      ep.interfaceNumber == ledInterfaceNumber;
      if (isKeyboardEndpoint && (dev->keyboardLedDirty || dev->keyboardLedPending))
      {
        ep.resubmitAfterLed = true;
      }
      else
      {
        ep.resubmitPending = true;
      }
    }
    for (EndpointState &ep : endpoints_)
    {
      if (!ep.inUse || !ep.resubmitPending)
      {
        continue;
      }
      ep.resubmitPending = false;
      if (ep.transfer && running_ && ep.deviceHandle)
      {
        submitInputTransfer(ep);
      }
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
        submitInputTransfer(ep);
      }
    }
    if (millis() - lastHostDeviceScanMs_ >= 500)
    {
      lastHostDeviceScanMs_ = millis();
      scanHostDevices();
    }
  }
  if (drainClientTransfers(2000))
  {
    const uint32_t releaseStartedAtMs = millis();
    while (!releaseClientResources() && millis() - releaseStartedAtMs < 1000)
    {
      usb_host_client_handle_events(clientHandle_, pdMS_TO_TICKS(5));
    }
    if (clientHandle_)
    {
      ESP_LOGW(TAG, "USB Host client resource release timed out");
      setLastError(ESP_ERR_TIMEOUT);
    }
  }
  else
  {
    ESP_LOGW(TAG, "USB Host transfer drain timed out; keeping client resources allocated");
    setLastError(ESP_ERR_TIMEOUT);
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
    if (running_)
    {
      handleNewDevice(eventMsg->new_dev.address);
    }
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
    resetDeviceState(*device);
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
    resetDeviceState(*device);
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
  // hasMidiInterface is its own flag rather than part of hasAudio: a MIDI
  // streaming interface is Audio class but has no streaming endpoint, feature
  // unit or sample rate, so none of the audio detection sees it. Leaving it out
  // here reported a working MIDI keyboard as unsupported.
  device->info.supported = isHub || hasHid || hasCdc || hasAudio || hasMsc ||
                           device->hasMidiInterface || device->vendorSerialSupported;
  device->info.isHub = isHub;
  dispatchDeviceConnected(device->info);
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
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  if (device->networkNetifAttached)
  {
    networkStopNetif(*device); // detach lwIP before the device handle goes away
  }
#endif
  device->hasNetworkInterface = false;
  device->networkLinkUp = false;
  networkDrainTx(*device); // wait out an in-flight send before tearing down
  vendorDrainOut(*device); // same for queued vendor bulk OUT transfers
  serialDrainOut(*device); // and for queued CDC serial OUT transfers
  releaseEndpoints(*device, false);
  device->disconnectPending = true;

  dispatchDeviceDisconnected(info);
  finalizeDisconnectedDevice(*device);
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
  // Identifying a hub means opening it, so with tracking off there is nothing to
  // scan for: leaving external hubs untouched is the whole point.
  if (!hubTrackingEnabled_)
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

    dispatchDeviceConnected(device->info);
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

  if (device.audioProtocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    // UAC2 keeps the supported sample rates in the Clock Source entity instead of
    // the format descriptor, so they need a class request. This runs on the USB
    // client task, hence the asynchronous transfer with its own callback.
    queryAudioClockSampleRates(device);
  }
}

size_t EspUsbHost::parseNetworkInterfaces(uint8_t address,
                                          const usb_config_desc_t *configDesc,
                                          EspUsbHostNetworkInterfaceInfo *interfaces,
                                          size_t maxInterfaces) const
{
  if (!configDesc || !interfaces || maxInterfaces == 0)
  {
    return 0;
  }

  size_t count = 0;
  uint8_t currentInterfaceNumber = 0xff;
  uint8_t currentInterfaceAlternate = 0;
  uint8_t currentInterfaceClass = 0;
  uint8_t currentInterfaceSubClass = 0;
  uint8_t currentControlIndex = 0xff;
  int dataInterfaceIndex[256];
  for (size_t i = 0; i < sizeof(dataInterfaceIndex) / sizeof(dataInterfaceIndex[0]); i++)
  {
    dataInterfaceIndex[i] = -1;
  }

  auto protocolForSubclass = [](uint8_t subclass) -> EspUsbHostNetworkProtocol
  {
    switch (subclass)
    {
    case USB_CDC_SUBCLASS_ECM:
      return ESP_USB_HOST_NETWORK_PROTOCOL_CDC_ECM;
    case USB_CDC_SUBCLASS_NCM:
      return ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM;
    default:
      return ESP_USB_HOST_NETWORK_PROTOCOL_NONE;
    }
  };

  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int offset = 0; offset < configDesc->wTotalLength;)
  {
    const uint8_t length = p[offset];
    if (length < 2 || offset + length > configDesc->wTotalLength)
    {
      break;
    }

    const uint8_t descriptorType = p[offset + 1];
    if (descriptorType == USB_INTERFACE_DESC && length >= sizeof(usb_intf_desc_t))
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[offset]);
      currentInterfaceNumber = intf->bInterfaceNumber;
      currentInterfaceAlternate = intf->bAlternateSetting;
      currentInterfaceClass = intf->bInterfaceClass;
      currentInterfaceSubClass = intf->bInterfaceSubClass;
      currentControlIndex = 0xff;

      const EspUsbHostNetworkProtocol protocol = protocolForSubclass(currentInterfaceSubClass);
      if (currentInterfaceClass == USB_CLASS_CDC_CONTROL_VALUE &&
          protocol != ESP_USB_HOST_NETWORK_PROTOCOL_NONE &&
          count < maxInterfaces)
      {
        EspUsbHostNetworkInterfaceInfo &network = interfaces[count];
        network = EspUsbHostNetworkInterfaceInfo();
        network.address = address;
        network.configurationValue = configDesc->bConfigurationValue;
        network.protocol = protocol;
        network.controlInterfaceNumber = currentInterfaceNumber;
        network.controlInterfaceAlternate = currentInterfaceAlternate;
        currentControlIndex = static_cast<uint8_t>(count);
        count++;
      }
      else if (currentInterfaceClass == USB_CLASS_CDC_DATA_VALUE)
      {
        int existing = dataInterfaceIndex[currentInterfaceNumber];
        if (existing < 0 && count < maxInterfaces)
        {
          EspUsbHostNetworkInterfaceInfo &network = interfaces[count];
          network = EspUsbHostNetworkInterfaceInfo();
          network.address = address;
          network.configurationValue = configDesc->bConfigurationValue;
          network.dataInterfaceNumber = currentInterfaceNumber;
          network.dataInterfaceAlternate = currentInterfaceAlternate;
          dataInterfaceIndex[currentInterfaceNumber] = static_cast<int>(count);
          count++;
        }
        else if (existing >= 0)
        {
          EspUsbHostNetworkInterfaceInfo &network = interfaces[existing];
          if (currentInterfaceAlternate > network.dataInterfaceAlternate)
          {
            network.dataInterfaceAlternate = currentInterfaceAlternate;
          }
        }
      }
    }
    else if (descriptorType == USB_CS_INTERFACE_DESC && length >= 3)
    {
      const uint8_t descriptorSubType = p[offset + 2];
      if (currentControlIndex != 0xff && currentControlIndex < count)
      {
        EspUsbHostNetworkInterfaceInfo &network = interfaces[currentControlIndex];
        if (descriptorSubType == USB_CDC_CS_UNION && length >= 5)
        {
          network.controlInterfaceNumber = p[offset + 3];
          network.dataInterfaceNumber = p[offset + 4];
          network.dataInterfaceAlternate = 0;
          dataInterfaceIndex[network.dataInterfaceNumber] = currentControlIndex;
        }
        else if (descriptorSubType == USB_CDC_CS_ETHERNET && length >= 7)
        {
          network.macAddressStringIndex = p[offset + 3];
          network.maxSegmentSize = static_cast<uint16_t>(p[offset + 5]) |
                                   (static_cast<uint16_t>(p[offset + 6]) << 8);
        }
        else if (descriptorSubType == USB_CDC_CS_NCM && length >= 6)
        {
          // NCM functional descriptor: bcdNcmVersion(3..5) bmNetworkCapabilities(5).
          network.ncmVersion = static_cast<uint16_t>(p[offset + 3]) |
                               (static_cast<uint16_t>(p[offset + 4]) << 8);
          network.networkCapabilities = p[offset + 5];
        }
      }
    }
    else if (descriptorType == USB_ENDPOINT_DESC && length >= sizeof(usb_ep_desc_t))
    {
      const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(&p[offset]);
      const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      const bool isInterrupt = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
      const bool isBulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;

      if (currentInterfaceClass == USB_CLASS_CDC_CONTROL_VALUE &&
          currentControlIndex != 0xff &&
          currentControlIndex < count &&
          isInterrupt &&
          isIn)
      {
        EspUsbHostNetworkInterfaceInfo &network = interfaces[currentControlIndex];
        network.notificationEndpoint = ep->bEndpointAddress;
        network.notificationMaxPacketSize = ep->wMaxPacketSize;
      }
      else if (currentInterfaceClass == USB_CLASS_CDC_DATA_VALUE && isBulk)
      {
        const int index = dataInterfaceIndex[currentInterfaceNumber];
        if (index >= 0 && static_cast<size_t>(index) < count)
        {
          EspUsbHostNetworkInterfaceInfo &network = interfaces[index];
          if (network.configurationValue == configDesc->bConfigurationValue &&
              network.dataInterfaceNumber == currentInterfaceNumber &&
              currentInterfaceAlternate >= network.dataInterfaceAlternate)
          {
            network.dataInterfaceAlternate = currentInterfaceAlternate;
            if (isIn)
            {
              network.inEndpoint = ep->bEndpointAddress;
              network.inMaxPacketSize = ep->wMaxPacketSize;
            }
            else
            {
              network.outEndpoint = ep->bEndpointAddress;
              network.outMaxPacketSize = ep->wMaxPacketSize;
            }
          }
        }
      }
    }

    offset += length;
  }

  return count;
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
    currentAudioTerminalLink_ = 0;
    currentMidiEndpointDirection_ = ESP_USB_HOST_MIDI_ENDPOINT_NONE;
    if (currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceProtocol_ == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
    {
      // Every interface of a UAC2 audio function repeats the revision in
      // bInterfaceProtocol. Latch it rather than assigning per interface: a device
      // with two audio functions would otherwise flip back to UAC1 while the UAC2
      // function's streaming interfaces are still being parsed.
      device->audioProtocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC2;
    }
    currentInterfaceClaimed_ = false;
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
    const bool isCdcAcmControlInterface = currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE &&
                                          currentInterfaceSubClass_ == USB_CDC_SUBCLASS_ACM;
    const bool isCdcAcmDataInterface = currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE &&
                                       device->hasCdcControlInterface &&
                                       !device->hasCdcDataInterface;
    if (currentInterfaceClass_ == USB_CLASS_HID_VALUE ||
        isCdcAcmControlInterface ||
        isCdcAcmDataInterface ||
        isAudioControlInterface ||
        isAudioInterface ||
        isMidiInterface ||
        isMscInterface ||
        isVendorSerialInterface)
    {
      currentClaimResult_ = usb_host_interface_claim(clientHandle_, device->handle, currentInterfaceNumber_, intf->bAlternateSetting);
      currentInterfaceClaimed_ = currentClaimResult_ == ESP_OK;
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
        if (isCdcAcmControlInterface)
        {
          device->hasCdcControlInterface = true;
          device->cdcControlInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC control interface ready: iface=%u", device->cdcControlInterfaceNumber);
          configureCdcAcm(*device);
        }
        else if (isCdcAcmDataInterface)
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
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING)
    {
      parseAudioStreamingDescriptor(*device, data);
    }
    break;
  }

  case USB_CS_ENDPOINT_DESC:
  {
    if (currentMidiEndpointDirection_ == ESP_USB_HOST_MIDI_ENDPOINT_NONE)
    {
      break;
    }
    const bool isIn = currentMidiEndpointDirection_ == ESP_USB_HOST_MIDI_ENDPOINT_IN;
    const uint8_t cables = espUsbHostMidiEndpointCableCount(data);
    if (cables == 0)
    {
      ESP_LOGW(TAG, "USB MIDI %s endpoint has no usable MS_GENERAL descriptor: iface=%u length=%u",
               isIn ? "IN" : "OUT",
               currentInterfaceNumber_,
               data[0]);
      break;
    }
    if (isIn)
    {
      device->midiInCableCount = cables;
    }
    else
    {
      device->midiOutCableCount = cables;
    }
    ESP_LOGI(TAG, "USB MIDI %s cables: iface=%u count=%u",
             isIn ? "IN" : "OUT",
             currentInterfaceNumber_,
             cables);
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
    // Only the descriptor immediately after a MIDI Streaming bulk endpoint may
    // claim its cables, so clear the latch before any of the branches below can
    // return.
    currentMidiEndpointDirection_ = ESP_USB_HOST_MIDI_ENDPOINT_NONE;
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

    const bool isSerialBulkEndpoint = currentInterfaceClaimed_ &&
                                      ((currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE &&
                                        device->hasCdcDataInterface &&
                                        currentInterfaceNumber_ == device->cdcDataInterfaceNumber) ||
                                       (currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE &&
                                        device->vendorSerialSupported &&
                                        device->hasVendorSerialInterface &&
                                        currentInterfaceNumber_ == device->vendorSerialInterfaceNumber)) &&
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

      endpoint->resubmitPending = true;
      ESP_LOGI(TAG, "%s bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
               currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(device->info.vid),
               endpoint->interfaceNumber,
               endpoint->address,
               ep->wMaxPacketSize);
      return;
    }

    // Only the endpoints of the one MIDI Streaming interface that was claimed
    // above. A device with a second MS interface leaves it unclaimed, and
    // currentClaimResult_ alone does not exclude it: it is reset to ESP_OK at
    // every interface descriptor and only overwritten where a claim is
    // attempted, so an unclaimed interface still reads as ESP_OK. Without the
    // interface number the later interface's endpoints would overwrite the
    // tracked ones and the fields of EspUsbHostMidiPortInfo could end up
    // describing two different interfaces.
    if (currentInterfaceClaimed_ &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_MIDI_STREAMING &&
        device->hasMidiInterface &&
        currentInterfaceNumber_ == device->midiInterfaceNumber &&
        isBulk)
    {
      currentMidiEndpointDirection_ = isIn ? ESP_USB_HOST_MIDI_ENDPOINT_IN
                                           : ESP_USB_HOST_MIDI_ENDPOINT_OUT;
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

      endpoint->resubmitPending = true;
      ESP_LOGI(TAG, "USB MIDI bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
               endpoint->interfaceNumber,
               endpoint->address,
               ep->wMaxPacketSize);
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
        isIsochronous)
    {
      static constexpr int AUDIO_ISOC_PACKETS = 8;
      if (espUsbHostAudioIsFeedbackEndpoint(ep->bmAttributes))
      {
        // Explicit feedback endpoint of an asynchronous playback interface. It
        // reports the device's rate estimate, not audio, so it must not be
        // registered as a stream (of either direction) or consume an endpoint slot.
        // Remember the one on the claimed alternate: while playback runs it is
        // polled to pace the OUT packets.
        if (currentInterfaceClaimed_)
        {
          device->audioOutFeedbackInterfaceNumber = currentInterfaceNumber_;
          device->audioOutFeedbackEndpointAddress = ep->bEndpointAddress;
          device->audioOutFeedbackPacketSize = ep->wMaxPacketSize;
          device->audioOutFeedbackInterval = ep->bInterval;
        }
        ESP_LOGI(TAG, "USB Audio feedback endpoint: iface=%u alt=%u ep=0x%02x size=%u interval=%u claimed=%u",
                 currentInterfaceNumber_,
                 currentInterfaceAlternate_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize,
                 ep->bInterval,
                 currentInterfaceClaimed_ ? 1u : 0u);
        return;
      }
      if (!currentInterfaceClaimed_)
      {
        // Only one alternate setting per interface is claimed, so the endpoints of
        // the others are not allocated in the host driver and can never carry a
        // transfer. Record the format they advertise -- a device that splits
        // 16-bit and 24-bit, or different rates, across alternates is describing
        // real capabilities -- but do not spend an endpoint slot or an isochronous
        // transfer buffer on them, and mark the stream as not startable.
        recordAudioStream(*device, ep, isIn, false);
        ESP_LOGI(TAG, "USB Audio format-only stream: iface=%u alt=%u ep=0x%02x (alternate not claimed)",
                 currentInterfaceNumber_,
                 currentInterfaceAlternate_,
                 ep->bEndpointAddress);
        return;
      }
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

    endpoint->resubmitPending = true;
    ESP_LOGI(TAG, "HID interrupt IN endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
             endpoint->interfaceNumber, endpoint->address, ep->wMaxPacketSize, ep->bInterval);
    break;
  }

  case USB_HID_DESC:
    // 0x21 is both the HID descriptor and the CCID class descriptor, so the
    // interface class decides how to read it.
    if (currentInterfaceClass_ == USB_CLASS_CCID_VALUE)
    {
      parseCcidClassDescriptor(*device, data);
    }
    else if (data[0] >= 9 && device->hidReportDescriptorCount < ESP_USB_HOST_MAX_HID_REPORT_DESCRIPTORS)
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
  if (!data || data[0] < 3)
  {
    return;
  }

  switch (data[2])
  {
  case USB_AUDIO_AC_HEADER:
    // bcdADC is the authoritative class revision. Prefer it over
    // bInterfaceProtocol, which some devices leave at 0 even for UAC2.
    if (data[0] >= 5 &&
        data[4] >= 0x02 &&
        device.audioProtocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
    {
      device.audioProtocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC2;
      ESP_LOGI(TAG, "USB Audio class revision from bcdADC: iface=%u bcdADC=0x%02x%02x",
               currentInterfaceNumber_,
               data[4],
               data[3]);
    }
    break;
  case USB_AUDIO_AC_CLOCK_SOURCE:
    parseAudioClockSourceDescriptor(device, data);
    break;
  case USB_AUDIO_AC_INPUT_TERMINAL:
    parseAudioTerminalDescriptor(device, data, true);
    break;
  case USB_AUDIO_AC_OUTPUT_TERMINAL:
    parseAudioTerminalDescriptor(device, data, false);
    break;
  case USB_AUDIO_AC_FEATURE_UNIT:
    parseAudioFeatureUnitDescriptor(device, data);
    break;
  default:
    break;
  }
}

void EspUsbHost::parseAudioClockSourceDescriptor(DeviceState &device, const uint8_t *data)
{
  // CLOCK_SOURCE only exists in UAC2: bClockID, bmAttributes, bmControls,
  // bAssocTerminal, iClockSource.
  if (device.audioProtocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 || data[0] < 6)
  {
    return;
  }

  const uint8_t clockSourceId = data[3];
  if (clockSourceId == 0)
  {
    return;
  }

  AudioClockSourceState *clock = nullptr;
  for (uint8_t i = 0; i < device.audioClockSourceCount; i++)
  {
    if (device.audioClockSources[i].clockSourceId == clockSourceId)
    {
      clock = &device.audioClockSources[i];
      break;
    }
  }
  if (!clock)
  {
    if (device.audioClockSourceCount >= ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES)
    {
      ESP_LOGD(TAG, "USB Audio Clock Source ignored, no slots: clock=%u", clockSourceId);
      return;
    }
    clock = &device.audioClockSources[device.audioClockSourceCount++];
  }

  clock->clockSourceId = clockSourceId;
  clock->attributes = data[4];
  clock->controls = data[5];
  ESP_LOGI(TAG, "USB Audio Clock Source: iface=%u clock=%u attributes=0x%02x controls=0x%02x",
           currentInterfaceNumber_,
           clock->clockSourceId,
           clock->attributes,
           clock->controls);
}

void EspUsbHost::parseAudioTerminalDescriptor(DeviceState &device, const uint8_t *data, bool input)
{
  // UAC2 terminals name the clock that drives them: bCSourceID sits at offset 7
  // in an Input Terminal and at offset 8 in an Output Terminal (after bSourceID).
  // UAC1 terminals have no clock field.
  if (device.audioProtocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    return;
  }
  const uint8_t clockOffset = input ? 7 : 8;
  if (data[0] <= clockOffset)
  {
    return;
  }

  const uint8_t terminalId = data[3];
  const uint8_t clockSourceId = data[clockOffset];
  if (terminalId == 0 || clockSourceId == 0)
  {
    return;
  }

  for (uint8_t i = 0; i < device.audioTerminalClockCount; i++)
  {
    if (device.audioTerminalClocks[i].terminalId == terminalId)
    {
      device.audioTerminalClocks[i].clockSourceId = clockSourceId;
      return;
    }
  }
  if (device.audioTerminalClockCount >= ESP_USB_HOST_MAX_AUDIO_TERMINALS)
  {
    ESP_LOGD(TAG, "USB Audio terminal clock link ignored, no slots: terminal=%u", terminalId);
    return;
  }
  AudioTerminalClockLink &link = device.audioTerminalClocks[device.audioTerminalClockCount++];
  link.terminalId = terminalId;
  link.clockSourceId = clockSourceId;
  ESP_LOGI(TAG, "USB Audio terminal clock link: iface=%u terminal=%u clock=%u",
           currentInterfaceNumber_,
           terminalId,
           clockSourceId);
}

void EspUsbHost::parseAudioStreamingDescriptor(DeviceState &device, const uint8_t *data)
{
  if (data[0] < 4)
  {
    return;
  }
  const bool uac2 = device.audioProtocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2;

  if (data[2] == USB_AUDIO_CS_AS_GENERAL)
  {
    // bTerminalLink is at the same offset in both revisions. UAC2 additionally
    // carries bNrChannels here, because its Format Type I descriptor dropped it.
    currentAudioTerminalLink_ = data[3];
    if (uac2 && data[0] >= 16)
    {
      currentAudioChannels_ = data[10];
    }
    return;
  }

  if (data[2] != USB_AUDIO_CS_AS_FORMAT_TYPE || data[3] != USB_AUDIO_FORMAT_TYPE_I)
  {
    return;
  }

  if (uac2)
  {
    // UAC2 Format Type I is bSubslotSize + bBitResolution only; the channel count
    // came from AS_GENERAL and the sample rates live in the Clock Source entity,
    // so they are filled in later by the SAM_FREQ RANGE query.
    if (data[0] < 6)
    {
      return;
    }
    currentAudioBytesPerSample_ = data[4];
    currentAudioBitsPerSample_ = data[5];
    ESP_LOGI(TAG, "USB Audio UAC2 Type I format: iface=%u channels=%u bytes=%u bits=%u terminal=%u",
             currentInterfaceNumber_,
             currentAudioChannels_,
             currentAudioBytesPerSample_,
             currentAudioBitsPerSample_,
             currentAudioTerminalLink_);
    return;
  }

  if (data[0] < 8)
  {
    return;
  }
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

void EspUsbHost::parseAudioFeatureUnitDescriptor(DeviceState &device, const uint8_t *data)
{
  if (data[0] < 7)
  {
    return;
  }

  const uint8_t unitId = data[3];
  const uint8_t sourceId = data[4];
  const EspUsbHostAudioFeatureUnitLayout layout = espUsbHostAudioFeatureUnitLayout(data, device.audioProtocol);
  if (unitId == 0 || !layout.valid)
  {
    return;
  }
  const uint8_t controlSize = layout.controlSize;
  const uint8_t controlOffset = layout.controlOffset;
  const uint8_t descriptorChannelCount = layout.channelCount;

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
  unit->protocol = device.audioProtocol;
  unit->channelCount = descriptorChannelCount < ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS
                           ? descriptorChannelCount
                           : ESP_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS;

  auto readControls = [&](uint8_t controlIndex) -> uint32_t
  {
    uint32_t controls = 0;
    const uint8_t *controlData = &data[controlOffset + controlIndex * controlSize];
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

void EspUsbHost::recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input, bool startable)
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
  info.startable = startable;
  info.protocol = device.audioProtocol;
  info.terminalLink = currentAudioTerminalLink_;
  if (device.audioProtocol == ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    info.clockSourceId = resolveAudioClockSource(device, currentAudioTerminalLink_);
  }
}

const EspUsbHost::AudioClockSourceState *EspUsbHost::findAudioClockSource(const DeviceState &device,
                                                                         uint8_t clockSourceId) const
{
  for (uint8_t i = 0; i < device.audioClockSourceCount; i++)
  {
    if (device.audioClockSources[i].clockSourceId == clockSourceId)
    {
      return &device.audioClockSources[i];
    }
  }
  return nullptr;
}

uint8_t EspUsbHost::resolveAudioClockSource(const DeviceState &device, uint8_t terminalLink) const
{
  for (uint8_t i = 0; i < device.audioTerminalClockCount; i++)
  {
    const AudioTerminalClockLink &link = device.audioTerminalClocks[i];
    if (link.terminalId == terminalLink && findAudioClockSource(device, link.clockSourceId))
    {
      return link.clockSourceId;
    }
  }
  // Devices with a single clock entity are common enough that falling back to it
  // is more useful than giving up when the terminal link cannot be matched.
  if (device.audioClockSourceCount == 1)
  {
    return device.audioClockSources[0].clockSourceId;
  }
  return 0;
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

  espUsbHostCacheSyncBeforeInTransfer(endpoint.transfer);

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
  if (!device.hidInputFields)
  {
    device.hidInputFields = static_cast<HIDInputFieldState *>(calloc(ESP_USB_HOST_MAX_HID_INPUT_FIELDS,
                                                                     sizeof(HIDInputFieldState)));
    if (!device.hidInputFields)
    {
      // Keyboard bitmap layout detection below does not depend on this table,
      // so continue parsing while omitting generic HID field metadata.
      ESP_LOGW(TAG, "HID input field allocation failed");
      setLastError(ESP_ERR_NO_MEM);
    }
  }

  size_t writeIndex = device.hidInputFieldCount;
  while (device.hidInputFields && writeIndex > 0 &&
         device.hidInputFields[writeIndex - 1].interfaceNumber == descriptor.interfaceNumber)
  {
    writeIndex--;
  }
  for (size_t i = writeIndex; i < device.hidInputFieldCount; i++)
  {
    device.hidInputFields[i] = {};
  }
  device.hidInputFieldCount = writeIndex;

  // Clear any keyboard input-report layout previously learned from this
  // interface, so a re-parse starts fresh.
  if (device.keyboardLayoutInterface == descriptor.interfaceNumber)
  {
    device.keyboardBitmapReport = false;
    device.keyboardHasModifierField = false;
    device.keyboardLayoutReportId = 0;
    device.keyboardModifierBitOffset = 0;
    device.keyboardBitmapBitOffset = 0;
    device.keyboardBitmapBitCount = 0;
    device.keyboardBitmapUsageMin = 0;
    device.keyboardLayoutInterface = 0xff;
  }
  if (device.keyboardLedInterface == descriptor.interfaceNumber)
  {
    device.hasKeyboardLedOutput = false;
    device.keyboardLedInterface = 0xff;
    device.keyboardLedReportId = 0;
  }

  // Locate the mouse report fields. Unlike the keyboard layout below this needs
  // collection tracking (Generic Desktop X / Y also appear in joysticks and
  // gamepads), so it is a separate walk over the descriptor in a header that
  // host tests can compile.
  if (device.mouseLayoutInterface == descriptor.interfaceNumber)
  {
    device.mouseLayout = EspUsbHostMouseReportLayout();
    device.mouseLayoutInterface = 0xff;
  }
  if (!device.mouseLayout.valid)
  {
    EspUsbHostMouseReportLayout mouseLayout;
    if (espUsbHostParseMouseReportLayout(descriptor.data, descriptor.length, mouseLayout))
    {
      device.mouseLayout = mouseLayout;
      device.mouseLayoutInterface = descriptor.interfaceNumber;
      ESP_LOGD(TAG,
               "Mouse layout iface=%u reportId=%u buttons=%u@%u x=%u@%u y=%u@%u wheel=%u@%u pan=%u@%u",
               descriptor.interfaceNumber,
               mouseLayout.reportId,
               mouseLayout.buttonCount,
               mouseLayout.buttonsBitOffset,
               mouseLayout.x.bitSize,
               mouseLayout.x.bitOffset,
               mouseLayout.y.bitSize,
               mouseLayout.y.bitOffset,
               mouseLayout.wheel.bitSize,
               mouseLayout.wheel.bitOffset,
               mouseLayout.pan.bitSize,
               mouseLayout.pan.bitOffset);
    }
  }

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
      const uint16_t fieldStartBit = bitOffset;

      for (uint8_t field = 0; field < reportCount; field++)
      {
        if (device.hidInputFields && !constant && reportSize > 0 &&
            device.hidInputFieldCount < ESP_USB_HOST_MAX_HID_INPUT_FIELDS)
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

      // Learn the keyboard input-report layout so NKRO bitmap reports can be
      // decoded. Keyboard usage page, 1-bit variable fields: a run at 0xE0-0xE7
      // is the modifier byte; a large run starting near usage 0 is the NKRO key
      // bitmap (one bit per usage, no 6-key rollover limit).
      if (global.usagePage == ESP_USB_HOST_HID_USAGE_PAGE_KEYBOARD && !constant &&
          reportSize == 1 && (flags & 0x02) != 0)
      {
        const uint16_t uMin = hasUsageRange ? usageMinimum : (usageCount > 0 ? usages[0] : 0);
        const uint16_t uMax = hasUsageRange ? usageMaximum
                                            : (usageCount > 0 ? usages[usageCount - 1] : 0);
        if (uMin >= 0xE0 && uMax <= 0xE7)
        {
          device.keyboardHasModifierField = true;
          device.keyboardModifierBitOffset = fieldStartBit;
          device.keyboardLayoutInterface = descriptor.interfaceNumber;
          device.keyboardLayoutReportId = global.reportId;
        }
        else if (reportCount >= 16 && uMin <= 0x04)
        {
          device.keyboardBitmapReport = true;
          device.keyboardBitmapBitOffset = fieldStartBit;
          device.keyboardBitmapBitCount = reportCount;
          device.keyboardBitmapUsageMin = uMin;
          device.keyboardLayoutInterface = descriptor.interfaceNumber;
          device.keyboardLayoutReportId = global.reportId;
        }
      }

      usageCount = 0;
      usageMinimum = 0;
      usageMaximum = 0;
      hasUsageRange = false;
    }
    else if (type == 0 && tag == 0x09)
    {
      // Output main item. A variable field on the LED usage page covering the
      // lock LEDs (Num Lock = 0x01 .. Kana = 0x05) is the keyboard LED report;
      // remember its interface and report ID so Set_Report can target keyboards
      // that never declare a boot interface. Boot keyboards keep report ID 0.
      const uint8_t flags = static_cast<uint8_t>(unsignedValue & 0xff);
      const bool constant = (flags & 0x01) != 0;
      if (!constant && !device.hasKeyboardLedOutput &&
          global.usagePage == ESP_USB_HOST_HID_USAGE_PAGE_LED)
      {
        const uint16_t uMin = hasUsageRange ? usageMinimum : (usageCount > 0 ? usages[0] : 0);
        if (uMin >= 0x01 && uMin <= 0x05)
        {
          device.hasKeyboardLedOutput = true;
          device.keyboardLedInterface = descriptor.interfaceNumber;
          device.keyboardLedReportId = global.reportId;
        }
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

  // Boot keyboards get their LED state pushed right after enumeration; keyboards
  // recognized only here (no boot interface) get the same initial sync once the
  // LED output report is known. The client task loop picks up the dirty flag.
  if (!device.hasKeyboardInterface && device.hasKeyboardLedOutput &&
      device.keyboardLedInterface == descriptor.interfaceNumber)
  {
    device.keyboardLedDirty = true;
    device.keyboardLedDirtyTimeMs = millis();
  }
}

bool EspUsbHost::hasHIDReportId(const DeviceState &device, uint8_t interfaceNumber, uint8_t reportId) const
{
  for (size_t i = 0; i < device.hidInputFieldCount; i++)
  {
    const HIDInputFieldState &inputField = device.hidInputFields[i];
    if (inputField.interfaceNumber == interfaceNumber && inputField.reportId == reportId)
    {
      return true;
    }
  }
  return false;
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
    const uint32_t rawValue = espUsbHostHidExtractBits(data, length, inputField.bitOffset, inputField.bitSize);
    value.value = inputField.logicalMin < 0 ? espUsbHostHidSignExtend(rawValue, inputField.bitSize)
                                            : static_cast<int32_t>(rawValue);
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
        endpoint.transferSubmitted ||
        endpoint.resubmitPending)
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

    endpoint.resubmitPending = true;
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
    if (device && deviceHasKeyboard(*device))
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
  if (!host)
  {
    usb_host_transfer_free(transfer);
    return;
  }

  // Pool membership, not the device handle, decides ownership: on disconnect the
  // device slot can already be reset by the time these canceled transfers are
  // dispatched. A transfer that belongs to no pool came from the one-shot
  // sendSerial() path and is freed here as before.
  DeviceState *device = nullptr;
  int slot = -1;
  for (DeviceState &candidate : host->devices_)
  {
    const int found = host->serialOutSlotOfTransfer(candidate, transfer);
    if (found >= 0)
    {
      device = &candidate;
      slot = found;
      break;
    }
  }

  if (!device)
  {
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
    {
      ESP_LOGD(TAG, "serial OUT transfer status=%d ep=0x%02x",
               transfer->status,
               transfer->bEndpointAddress);
      host->setLastError(ESP_FAIL);
    }
    // Either a one-shot transfer, or a pooled one whose pool was already
    // released; releaseSerialOutQueue() deliberately leaked the latter, so both
    // are freed now that the driver is done with them.
    usb_host_transfer_free(transfer);
    return;
  }

  EspUsbHostSerialWriteStats &stats = device->serialWriteStats;
  stats.completed++;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    stats.bytes += static_cast<uint64_t>(transfer->actual_num_bytes);
  }
  else
  {
    stats.errors++;
    ESP_LOGD(TAG, "serial OUT status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    host->setLastError(ESP_FAIL);
    if (transfer->status != USB_TRANSFER_STATUS_CANCELED)
    {
      device->serialOutHalted = true;
    }
  }

  portENTER_CRITICAL(&host->serialOutMux_);
  device->serialOutSlotState[slot] = SERIAL_OUT_SLOT_FREE;
  portEXIT_CRITICAL(&host->serialOutMux_);
  if (device->serialOutFreeSlots)
  {
    xSemaphoreGive(device->serialOutFreeSlots);
  }
}

void EspUsbHost::handleTransfer(usb_transfer_t *transfer)
{
  EndpointState *endpoint = findEndpoint(transfer->device_handle, transfer->bEndpointAddress);
  if (!endpoint)
  {
    return;
  }
  endpoint->transferSubmitted = false;
  DeviceState *device = findDeviceByHandle(endpoint->deviceHandle);

  const bool isAudioStreaming = endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
                                endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
      (transfer->actual_num_bytes > 0 || isAudioStreaming))
  {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE
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
      if (device && device->keyboardBitmapReport &&
          endpoint->interfaceNumber == device->keyboardLayoutInterface)
      {
        // NKRO keyboard: keys arrive as a bitmap, not the 8-byte boot report.
        handleKeyboardBitmap(*endpoint, *device, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (device && device->mouseLayout.valid &&
               endpoint->interfaceNumber == device->mouseLayoutInterface &&
               (device->mouseLayout.reportId == 0 ||
                (transfer->actual_num_bytes >= 2 &&
                 transfer->data_buffer[0] == device->mouseLayout.reportId)))
      {
        // Report ID taken from the descriptor rather than assumed, so a mouse
        // report that shares an interface with other reports is recognized
        // whatever ID it was given. A descriptor with no report IDs declares
        // exactly one input report, so on that interface every report is this
        // one -- no protocol or first-byte guessing needed.
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (device &&
               endpoint->interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
               transfer->actual_num_bytes >= 5 &&
               transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE &&
               hasHIDReportId(*device, endpoint->interfaceNumber, ESP_USB_HOST_HID_REPORT_ID_MOUSE))
      {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 9 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_KEYBOARD)
      {
        handleKeyboard(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1, transfer->data_buffer, transfer->actual_num_bytes);
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
    else if (device && device->hasNetworkInterface &&
             endpoint->address == device->networkInterface.inEndpoint)
    {
      // NCM data interface is CDC-DATA class (0x0a) like CDC-ACM serial, so this
      // must be checked before the serial branch below.
      handleNetworkInput(*device, *endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (device && device->hasNetworkInterface &&
             endpoint->address == device->networkInterface.notificationEndpoint)
    {
      handleNetworkNotification(*device, transfer->data_buffer, transfer->actual_num_bytes);
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
    else if (device && endpoint->interfaceClass == USB_CLASS_CCID_VALUE)
    {
      // Only the CCID interrupt IN endpoint is managed here; the bulk pipes are
      // driven synchronously by the ccid*() calls.
      handleCcidNotification(*device, transfer->data_buffer, transfer->actual_num_bytes);
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

  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    endpoint->recoveryPending = true;
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
    uint8_t ledInterfaceNumber = 0;
    uint8_t ledReportId = 0;
    const bool isKeyboardEndpoint = device &&
                                    keyboardLedTarget(*device, ledInterfaceNumber, ledReportId) &&
                                    endpoint->interfaceNumber == ledInterfaceNumber;
    if (isKeyboardEndpoint && device->keyboardLedDirty)
    {
      endpoint->resubmitAfterLed = true;
    }
    else
    {
      endpoint->resubmitPending = true;
    }
  }
}

void EspUsbHost::dispatchKeyboardState(EndpointState &endpoint,
                                       DeviceState *device,
                                       const uint8_t *bitmap,
                                       const uint8_t *rawData,
                                       size_t rawLength,
                                       const uint8_t *reportData,
                                       size_t reportLength)
{
  if (!bitmap)
  {
    return;
  }

  EspUsbHostKeyboardState state;
  bool changed = false;
  for (size_t i = 0; i < ESP_USB_HOST_KEYBOARD_BITMAP_SIZE; i++)
  {
    state.bitmap[i] = bitmap[i];
    state.changedBitmap[i] = static_cast<uint8_t>(bitmap[i] ^ endpoint.lastKeyboardState[i]);
    changed = changed || state.changedBitmap[i] != 0;
  }
  memcpy(endpoint.lastKeyboardState, bitmap, ESP_USB_HOST_KEYBOARD_BITMAP_SIZE);

  if (!changed)
  {
    return;
  }

  std::shared_ptr<KeyboardStateCallback> singleCallback;
  std::shared_ptr<KeyboardStateCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount = snapshotHIDCallbacks(keyboardStateCallback_,
                                                    keyboardStateListeners_,
                                                    singleCallback,
                                                    listeners);
  if (!singleCallback && listenerCount == 0)
  {
    return;
  }

  state.address = endpoint.deviceAddress;
  state.interfaceNumber = endpoint.interfaceNumber;
  if (device)
  {
    state.vid = device->info.vid;
    state.pid = device->info.pid;
    state.manufacturer = device->info.manufacturer;
    state.product = device->info.product;
    state.serial = device->info.serial;
    state.numLock = device->keyboardNumLock;
    state.capsLock = device->keyboardCapsLock;
    state.scrollLock = device->keyboardScrollLock;
  }
  for (int b = 0; b < 8; b++)
  {
    if (state.isDown(static_cast<uint8_t>(0xe0 + b)))
    {
      state.modifiers |= static_cast<uint8_t>(1u << b);
    }
  }
  state.rawData = rawData;
  state.rawLength = rawLength;
  state.reportData = reportData;
  state.reportLength = reportLength;
  invokeHIDCallbacks(singleCallback, listeners, listenerCount, state);
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

  uint8_t keyboardState[ESP_USB_HOST_KEYBOARD_BITMAP_SIZE] = {};
  for (int b = 0; b < 8; b++)
  {
    if ((currentReport.data[0] & static_cast<uint8_t>(1u << b)) != 0)
    {
      const uint8_t usage = static_cast<uint8_t>(0xe0 + b);
      keyboardState[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
    }
  }
  for (int i = 0; i < 6; i++)
  {
    const uint8_t usage = currentReport.data[2 + i];
    if (usage != 0)
    {
      keyboardState[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
    }
  }
  dispatchKeyboardState(endpoint, device, keyboardState, rawData, rawLength, data, length);

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
    std::shared_ptr<KeyboardCallback> singleCallback;
    std::shared_ptr<KeyboardCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
    const size_t listenerCount = snapshotHIDCallbacks(keyboardCallback_,
                                                      keyboardListeners_,
                                                      singleCallback,
                                                      listeners);
    invokeHIDCallbacks(singleCallback, listeners, listenerCount, events[i]);
  }

  memcpy(endpoint.lastKeyboardReport, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
}

// Decode an NKRO keyboard input report (report protocol): a modifier byte plus a
// key bitmap (one bit per usage, no 6-key rollover limit). The bit offsets and
// range were learned from the HID report descriptor in parseHIDReportDescriptor.
// Press/release events are emitted by diffing against the previous bitmap.
void EspUsbHost::handleKeyboardBitmap(EndpointState &endpoint, DeviceState &device, const uint8_t *data, size_t length)
{
  if (!data || length == 0)
  {
    return;
  }

  // Strip a report-ID prefix if the descriptor declared one; bit offsets are
  // relative to the report body after that byte.
  const uint8_t *body = data;
  size_t bodyLen = length;
  if (device.keyboardLayoutReportId != 0)
  {
    if (length < 2 || data[0] != device.keyboardLayoutReportId)
    {
      return;
    }
    body = data + 1;
    bodyLen = length - 1;
  }

  const uint16_t bitStart = device.keyboardBitmapBitOffset;
  const uint16_t bitCount = device.keyboardBitmapBitCount;
  if (bitCount == 0 || ((bitCount + 7) / 8) > ESP_USB_HOST_NKRO_BITMAP_MAX_BYTES)
  {
    return;
  }
  if (static_cast<size_t>(bitStart) + bitCount > bodyLen * 8)
  {
    return; // report shorter than the declared bitmap
  }

  auto readBit = [](const uint8_t *buf, size_t bit) -> bool
  {
    return (buf[bit >> 3] >> (bit & 7)) & 0x01;
  };

  // Modifier byte: 8 contiguous 1-bit fields (usages 0xE0-0xE7 -> bits 0-7).
  uint8_t modifiers = 0;
  if (device.keyboardHasModifierField &&
      static_cast<size_t>(device.keyboardModifierBitOffset) + 8 <= bodyLen * 8)
  {
    for (int b = 0; b < 8; b++)
    {
      if (readBit(body, device.keyboardModifierBitOffset + b))
      {
        modifiers |= static_cast<uint8_t>(1u << b);
      }
    }
  }

  const bool hadPrev = endpoint.keyboardBitmapReady;
  const uint8_t prevModifiers = endpoint.lastKeyboardBitmapModifiers;
  auto prevBit = [&](uint16_t i) -> bool
  {
    return hadPrev && ((endpoint.lastKeyboardBitmap[i >> 3] >> (i & 7)) & 0x01);
  };

  // Toggle lock LEDs on newly-pressed lock keys (same as the boot path).
  bool ledChanged = false;
  for (uint16_t i = 0; i < bitCount; i++)
  {
    if (!readBit(body, bitStart + i) || prevBit(i))
    {
      continue; // not a fresh press
    }
    const uint8_t usage = static_cast<uint8_t>(device.keyboardBitmapUsageMin + i);
    if (usage == HID_KEY_NUM_LOCK)
    {
      device.keyboardNumLock = !device.keyboardNumLock;
      ledChanged = true;
    }
    else if (usage == HID_KEY_CAPS_LOCK)
    {
      device.keyboardCapsLock = !device.keyboardCapsLock;
      ledChanged = true;
    }
    else if (usage == HID_KEY_SCROLL_LOCK)
    {
      device.keyboardScrollLock = !device.keyboardScrollLock;
      ledChanged = true;
    }
  }
  if (ledChanged)
  {
    device.keyboardLedDirty = true;
    device.keyboardLedDirtyTimeMs = millis();
  }

  const bool capsLock = device.keyboardCapsLock;
  const bool numLock = device.keyboardNumLock;
  const bool scrollLock = device.keyboardScrollLock;

  uint8_t keyboardState[ESP_USB_HOST_KEYBOARD_BITMAP_SIZE] = {};
  for (uint16_t i = 0; i < bitCount; i++)
  {
    const uint16_t usage = static_cast<uint16_t>(device.keyboardBitmapUsageMin + i);
    if (usage > 0xff || !readBit(body, bitStart + i))
    {
      continue;
    }
    keyboardState[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
  }
  if (device.keyboardHasModifierField)
  {
    // A dedicated modifier field is authoritative when a descriptor also
    // includes 0xE0-0xE7 in its general key bitmap.
    for (int b = 0; b < 8; b++)
    {
      const uint8_t usage = static_cast<uint8_t>(0xe0 + b);
      keyboardState[usage >> 3] &= static_cast<uint8_t>(~(1u << (usage & 7)));
      if ((modifiers & static_cast<uint8_t>(1u << b)) != 0)
      {
        keyboardState[usage >> 3] |= static_cast<uint8_t>(1u << (usage & 7));
      }
    }
  }
  dispatchKeyboardState(endpoint, &device, keyboardState, data, length, body, bodyLen);

  // Emit a press/release event for every changed key bit (no 6-key cap).
  for (uint16_t i = 0; i < bitCount; i++)
  {
    const bool now = readBit(body, bitStart + i);
    if (now == prevBit(i))
    {
      continue;
    }
    const uint8_t usage = static_cast<uint8_t>(device.keyboardBitmapUsageMin + i);
    if (usage == 0)
    {
      continue;
    }
    EspUsbHostKeyboardEvent event;
    event.interfaceNumber = endpoint.interfaceNumber;
    event.address = endpoint.deviceAddress;
    event.vid = device.info.vid;
    event.pid = device.info.pid;
    event.manufacturer = device.info.manufacturer;
    event.product = device.info.product;
    event.serial = device.info.serial;
    event.pressed = now;
    event.released = !now;
    event.keycode = usage;
    event.unicode = espUsbHostKeycodeToUnicode(usage, now ? modifiers : prevModifiers, keyboardLayout_, capsLock, numLock);
    event.ascii = event.unicode <= 0xFF ? static_cast<uint8_t>(event.unicode) : 0;
    event.modifiers = now ? modifiers : prevModifiers;
    event.numLock = numLock;
    event.capsLock = capsLock;
    event.scrollLock = scrollLock;
    event.rawData = data;
    event.rawLength = length;
    event.reportData = body;
    event.reportLength = bodyLen;
    ESP_LOGD(TAG, "Keyboard(NKRO) %s iface=%u keycode=0x%02x ascii=0x%02x modifiers=0x%02x caps=%d num=%d scroll=%d",
             now ? "press" : "release", event.interfaceNumber, event.keycode, event.ascii,
             event.modifiers, capsLock, numLock, scrollLock);
    std::shared_ptr<KeyboardCallback> singleCallback;
    std::shared_ptr<KeyboardCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
    const size_t listenerCount = snapshotHIDCallbacks(keyboardCallback_,
                                                      keyboardListeners_,
                                                      singleCallback,
                                                      listeners);
    invokeHIDCallbacks(singleCallback, listeners, listenerCount, event);
  }

  // Save the current bitmap (densely, bit i -> our bit i) and modifiers.
  memset(endpoint.lastKeyboardBitmap, 0, sizeof(endpoint.lastKeyboardBitmap));
  for (uint16_t i = 0; i < bitCount; i++)
  {
    if (readBit(body, bitStart + i))
    {
      endpoint.lastKeyboardBitmap[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
    }
  }
  endpoint.lastKeyboardBitmapModifiers = modifiers;
  endpoint.keyboardBitmapReady = true;
}

void EspUsbHost::handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  std::shared_ptr<MouseCallback> singleCallback;
  std::shared_ptr<MouseCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount = snapshotHIDCallbacks(mouseCallback_, mouseListeners_, singleCallback, listeners);
  if (!singleCallback && listenerCount == 0)
  {
    return;
  }

  const uint8_t *rawData = data;
  const size_t rawLength = length;

  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  const bool useLayout = device && device->mouseLayout.valid &&
                         device->mouseLayoutInterface == endpoint.interfaceNumber;

  EspUsbHostMouseEvent event;
  if (useLayout)
  {
    // Report protocol: fields sit where the HID report descriptor says, which
    // for anything past a plain 3-button mouse is not the boot layout.
    EspUsbHostMouseReportValues values;
    if (!espUsbHostDecodeMouseReport(device->mouseLayout, data, length, values))
    {
      return;
    }
    if (device->mouseLayout.reportId != 0)
    {
      data += 1;
      length -= 1;
    }

    event.interfaceNumber = endpoint.interfaceNumber;
    event.x = static_cast<int16_t>(values.x);
    event.y = static_cast<int16_t>(values.y);
    event.wheel = static_cast<int16_t>(values.wheel);
    event.pan = static_cast<int16_t>(values.pan);
    event.buttonMask = values.buttons;
    event.previousButtonMask = endpoint.lastMouseButtonMask;
    event.buttonCount = device->mouseLayout.buttonCount;
    event.buttons = static_cast<uint8_t>(values.buttons & 0xff);
    event.previousButtons = static_cast<uint8_t>(endpoint.lastMouseButtonMask & 0xff);
    event.moved = event.x != 0 || event.y != 0 || event.wheel != 0 || event.pan != 0;
    event.buttonsChanged = event.buttonMask != event.previousButtonMask;
    if (!event.moved && !event.buttonsChanged)
    {
      return;
    }
  }
  else
  {
    if (endpoint.interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
        length >= 5 &&
        data[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE)
    {
      data += 1;
      length -= 1;
    }

    if (!espUsbHostParseBootMouseReport(endpoint.interfaceNumber,
                                        data,
                                        length,
                                        endpoint.lastMouseButtons,
                                        event))
    {
      return;
    }
    event.buttonMask = event.buttons;
    event.previousButtonMask = event.previousButtons;
  }
  event.address = endpoint.deviceAddress;
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

  ESP_LOGD(TAG, "Mouse iface=%u x=%d y=%d wheel=%d pan=%d buttons=0x%04x previous=0x%04x",
           event.interfaceNumber,
           event.x,
           event.y,
           event.wheel,
           event.pan,
           event.buttonMask,
           event.previousButtonMask);
  invokeHIDCallbacks(singleCallback, listeners, listenerCount, event);
  endpoint.lastMouseButtons = event.buttons;
  endpoint.lastMouseButtonMask = event.buttonMask;
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
  if (!data || length < 4)
  {
    return;
  }

  // Snapshot once per transfer, not per 4-byte packet: a bulk MIDI transfer can
  // carry many packets and taking the mutex for each one is pure overhead.
  std::shared_ptr<MidiMessageCallback> singleCallback;
  std::shared_ptr<MidiMessageCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount =
      snapshotHIDCallbacks(midiMessageCallback_, midiMessageListeners_, singleCallback, listeners);
  if (!singleCallback && listenerCount == 0)
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
    if (singleCallback)
    {
      (*singleCallback)(message);
    }
    for (size_t i = 0; i < listenerCount; i++)
    {
      (*listeners[i])(message);
    }
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
  std::shared_ptr<ConsumerControlCallback> singleCallback;
  std::shared_ptr<ConsumerControlCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount = snapshotHIDCallbacks(consumerControlCallback_,
                                                    consumerControlListeners_,
                                                    singleCallback,
                                                    listeners);
  if (!singleCallback && listenerCount == 0)
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
  invokeHIDCallbacks(singleCallback, listeners, listenerCount, event);
  endpoint.lastConsumerUsage = event.pressed ? event.usage : 0;
}

void EspUsbHost::handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length, const uint8_t *rawData, size_t rawLength)
{
  std::shared_ptr<GamepadCallback> singleCallback;
  std::shared_ptr<GamepadCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount = snapshotHIDCallbacks(gamepadCallback_, gamepadListeners_, singleCallback, listeners);
  if (!singleCallback && listenerCount == 0)
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
    if (!endpoint.hidFieldValues)
    {
      endpoint.hidFieldValues = static_cast<EspUsbHostHIDFieldValue *>(
          calloc(ESP_USB_HOST_MAX_HID_EVENT_FIELDS, sizeof(EspUsbHostHIDFieldValue)));
      if (!endpoint.hidFieldValues)
      {
        ESP_LOGW(TAG, "HID event field allocation failed");
        setLastError(ESP_ERR_NO_MEM);
      }
    }
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
  invokeHIDCallbacks(singleCallback, listeners, listenerCount, event);
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
  std::shared_ptr<SystemControlCallback> singleCallback;
  std::shared_ptr<SystemControlCallback> listeners[ESP_USB_HOST_MAX_LISTENERS_PER_EVENT];
  const size_t listenerCount = snapshotHIDCallbacks(systemControlCallback_,
                                                    systemControlListeners_,
                                                    singleCallback,
                                                    listeners);
  if (!singleCallback && listenerCount == 0)
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
  invokeHIDCallbacks(singleCallback, listeners, listenerCount, event);
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
      resetEndpointState(endpoint);
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
      resetDeviceState(device);
      return &device;
    }
  }
  return nullptr;
}

void EspUsbHost::resetDeviceState(DeviceState &device)
{
  // Free the reusable OUT transfer (it references this device's now-stale handle),
  // but keep the TX lock / completion semaphore alive across the reset: they are
  // created once per device slot and reused for whatever device next occupies it.
  // Deleting them here would risk a concurrent sender blocking on / signalling a
  // freed handle; a plain mutex + binary semaphore per slot is cheap to keep.
  if (device.networkOutTransfer)
  {
    usb_host_transfer_free(device.networkOutTransfer);
    device.networkOutTransfer = nullptr;
  }
  free(device.networkRxRing);
  free(device.networkAsm);
  free(device.hidInputFields);
  free(device.ccidBuffer);
  device.networkRxRing = nullptr;
  device.networkAsm = nullptr;
  device.hidInputFields = nullptr;
  device.ccidBuffer = nullptr;
  SemaphoreHandle_t txLock = device.networkTxLock;
  SemaphoreHandle_t outDone = device.networkOutDone;
  // Like the network TX lock, the CCID mutex belongs to the slot rather than to
  // the device: a caller blocked on it must never wake up on a freed handle.
  SemaphoreHandle_t ccidLock = device.ccidLock;
  device.~DeviceState();
  new (&device) DeviceState();
  device.networkTxLock = txLock;
  device.networkOutDone = outDone;
  device.ccidLock = ccidLock;
}

void EspUsbHost::resetEndpointState(EndpointState &endpoint)
{
  free(endpoint.hidFieldValues);
  endpoint.hidFieldValues = nullptr;
  endpoint.~EndpointState();
  new (&endpoint) EndpointState();
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
    if (!espUsbHostAudioFeatureHasControl(controls, controlSelector, unit.protocol))
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
    const bool hasMute = espUsbHostAudioFeatureHasControl(controls, USB_AUDIO_FEATURE_MUTE_CONTROL, unit.protocol);
    const bool hasVolume = espUsbHostAudioFeatureHasControl(controls, USB_AUDIO_FEATURE_VOLUME_CONTROL, unit.protocol);
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
    if (!device.inUse || !device.handle || !deviceHasKeyboard(device))
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

EspUsbHost::DeviceState *EspUsbHost::findNetworkDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findNetworkDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findNetworkDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasNetworkInterface)
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
      if (!vendorInterfaceEligible(device, intf, interfaceNumber))
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

size_t EspUsbHost::getNetworkInterfaces(uint8_t address,
                                        EspUsbHostNetworkInterfaceInfo *interfaces,
                                        size_t maxInterfaces)
{
  if (!interfaces || maxInterfaces == 0 || !running_ || !clientHandle_)
  {
    return 0;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "getNetworkInterfaces() cannot run from USB client task");
    return 0;
  }

  DeviceState *device = findDevice(address);
  if (!device || !device->handle)
  {
    return 0;
  }

  const usb_device_desc_t *devDesc = nullptr;
  esp_err_t err = usb_host_get_device_descriptor(device->handle, &devDesc);
  if (err != ESP_OK || !devDesc)
  {
    ESP_LOGW(TAG, "usb_host_get_device_descriptor(network scan) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return 0;
  }

  size_t count = 0;
  for (uint8_t configValue = 1; configValue <= devDesc->bNumConfigurations && count < maxInterfaces; configValue++)
  {
    const usb_config_desc_t *configDesc = nullptr;
    err = usb_host_get_config_desc(clientHandle_, device->handle, configValue, &configDesc);
    if (err != ESP_OK || !configDesc)
    {
      ESP_LOGD(TAG, "usb_host_get_config_desc(config=%u) failed: %s", configValue, esp_err_to_name(err));
      continue;
    }

    count += parseNetworkInterfaces(address,
                                    configDesc,
                                    &interfaces[count],
                                    maxInterfaces - count);
    usb_host_free_config_desc(configDesc);
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

bool EspUsbHost::drainClientTransfers(uint32_t timeoutMs)
{
  if (!clientHandle_)
  {
    return true;
  }

  // Stop every callback-driven producer before canceling its transfers. The
  // callbacks still need the client task below to receive CANCELED completions,
  // so do not free any transfer or deregister the client yet.
  for (DeviceState &device : devices_)
  {
    if (device.inUse)
    {
      device.audioOutRunning = false;
      device.usbVendorOutQueueActive = false;
    }
  }

  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse || !endpoint.transfer || !endpoint.transferSubmitted ||
        !endpoint.deviceHandle)
    {
      continue;
    }
    const esp_err_t haltErr = usb_host_endpoint_halt(endpoint.deviceHandle, endpoint.address);
    if (haltErr == ESP_OK || haltErr == ESP_ERR_INVALID_STATE)
    {
      const esp_err_t flushErr = usb_host_endpoint_flush(endpoint.deviceHandle, endpoint.address);
      if (flushErr != ESP_OK && flushErr != ESP_ERR_INVALID_STATE && flushErr != ESP_ERR_NOT_FOUND)
      {
        ESP_LOGD(TAG, "usb_host_endpoint_flush(shutdown ep=0x%02x) failed: %s",
                 endpoint.address,
                 esp_err_to_name(flushErr));
      }
    }
    else if (haltErr != ESP_ERR_NOT_FOUND)
    {
      ESP_LOGD(TAG, "usb_host_endpoint_halt(shutdown ep=0x%02x) failed: %s",
               endpoint.address,
               esp_err_to_name(haltErr));
    }
  }

  // Managed audio OUT transfers are not EndpointState entries.
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || device.audioOutEndpointAddress == 0)
    {
      continue;
    }
    bool hasAudioTransfer = false;
    for (usb_transfer_t *transfer : device.audioOutTransfers)
    {
      hasAudioTransfer = hasAudioTransfer || transfer != nullptr;
    }
    if (hasAudioTransfer)
    {
      usb_host_endpoint_halt(device.handle, device.audioOutEndpointAddress);
      usb_host_endpoint_flush(device.handle, device.audioOutEndpointAddress);
    }
  }

  // Neither is the explicit feedback IN transfer that paces them.
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.audioOutFeedbackTransfer)
    {
      continue;
    }
    usb_host_endpoint_halt(device.handle, device.audioOutFeedbackEndpointAddress);
    usb_host_endpoint_flush(device.handle, device.audioOutFeedbackEndpointAddress);
  }

  // Queued vendor bulk OUT transfers are not EndpointState entries either.
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || device.usbVendorOutEndpointAddress == 0)
    {
      continue;
    }
    if (vendorWritePending(device.info.address) != 0)
    {
      usb_host_endpoint_halt(device.handle, device.usbVendorOutEndpointAddress);
      usb_host_endpoint_flush(device.handle, device.usbVendorOutEndpointAddress);
    }
  }

  const uint32_t startedAtMs = millis();
  while (millis() - startedAtMs < timeoutMs)
  {
    const esp_err_t eventErr = usb_host_client_handle_events(clientHandle_, pdMS_TO_TICKS(5));
    if (eventErr != ESP_OK && eventErr != ESP_ERR_TIMEOUT)
    {
      ESP_LOGD(TAG, "usb_host_client_handle_events(shutdown) failed: %s", esp_err_to_name(eventErr));
    }

    bool idle = true;
    for (const EndpointState &endpoint : endpoints_)
    {
      if (endpoint.inUse && endpoint.transferSubmitted)
      {
        idle = false;
        break;
      }
    }
    for (DeviceState &device : devices_)
    {
      if (!device.inUse)
      {
        continue;
      }
      for (usb_transfer_t *transfer : device.audioOutTransfers)
      {
        if (transfer)
        {
          idle = false;
        }
      }
      if (device.audioOutFeedbackTransfer)
      {
        idle = false;
      }
      for (uint8_t i = 0; i < device.usbVendorOutQueueDepth; i++)
      {
        if (device.usbVendorOutSlotState[i] == VENDOR_OUT_SLOT_INFLIGHT)
        {
          idle = false;
          break;
        }
      }
      if (device.networkTxLock)
      {
        if (xSemaphoreTake(device.networkTxLock, 0) == pdTRUE)
        {
          xSemaphoreGive(device.networkTxLock);
        }
        else
        {
          idle = false;
        }
      }
    }
    if (idle)
    {
      // Dispatch any final callbacks queued in the same client event batch.
      usb_host_client_handle_events(clientHandle_, 0);
      return true;
    }
  }
  return false;
}

bool EspUsbHost::releaseClientResources()
{
  bool allDevicesClosed = true;
  for (DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    device.hasNetworkInterface = false;
    device.networkLinkUp = false;
    networkDrainTx(device);
    releaseEndpoints(device, false);

    uint8_t remainingInterfaces = 0;
    for (uint8_t i = 0; i < device.interfaceCount; i++)
    {
      const uint8_t interfaceNumber = device.interfaces[i];
      const esp_err_t releaseErr = usb_host_interface_release(clientHandle_, device.handle, interfaceNumber);
      if (releaseErr != ESP_OK && releaseErr != ESP_ERR_NOT_FOUND)
      {
        device.interfaces[remainingInterfaces++] = interfaceNumber;
      }
    }
    for (uint8_t i = remainingInterfaces; i < device.interfaceCount; i++)
    {
      device.interfaces[i] = 0;
    }
    device.interfaceCount = remainingInterfaces;
    if (remainingInterfaces != 0)
    {
      allDevicesClosed = false;
      continue;
    }
    device.endpointChannelCount = 0;

    if (device.handle)
    {
      const esp_err_t closeErr = usb_host_device_close(clientHandle_, device.handle);
      if (closeErr != ESP_OK && closeErr != ESP_ERR_NOT_FOUND)
      {
        allDevicesClosed = false;
        continue;
      }
    }
    resetDeviceState(device);
  }

  if (!allDevicesClosed)
  {
    return false;
  }
  if (clientHandle_)
  {
    const esp_err_t deregisterErr = usb_host_client_deregister(clientHandle_);
    if (deregisterErr != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_client_deregister() failed: %s", esp_err_to_name(deregisterErr));
      setLastError(deregisterErr);
      return false;
    }
    clientHandle_ = nullptr;
  }
  return true;
}

bool EspUsbHost::uninstallHostLibrary(uint32_t timeoutMs)
{
  esp_err_t freeErr = usb_host_device_free_all();
  bool allFree = freeErr == ESP_OK;
  if (freeErr != ESP_OK && freeErr != ESP_ERR_NOT_FINISHED)
  {
    ESP_LOGW(TAG, "usb_host_device_free_all() failed: %s", esp_err_to_name(freeErr));
    setLastError(freeErr);
    return false;
  }

  const uint32_t startedAtMs = millis();
  while (!allFree && millis() - startedAtMs < timeoutMs)
  {
    uint32_t eventFlags = 0;
    const esp_err_t eventErr = usb_host_lib_handle_events(pdMS_TO_TICKS(10), &eventFlags);
    if (eventErr != ESP_OK && eventErr != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_lib_handle_events(shutdown) failed: %s", esp_err_to_name(eventErr));
      setLastError(eventErr);
      return false;
    }
    allFree = (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0;
  }
  if (!allFree)
  {
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  // usb_host_uninstall() refuses with ESP_ERR_INVALID_STATE while the library
  // still holds an unread event flag or a pending process request, and the
  // client deregister above left USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS set. Only
  // usb_host_lib_handle_events() clears those, so pump it until uninstall is
  // accepted. With no device attached usb_host_device_free_all() returns ESP_OK
  // and the ALL_FREE loop never runs, which is what left the host library
  // installed and made every later begin() fail in usb_host_install().
  esp_err_t uninstallErr = usb_host_uninstall();
  while (uninstallErr == ESP_ERR_INVALID_STATE && millis() - startedAtMs < timeoutMs)
  {
    uint32_t eventFlags = 0;
    const esp_err_t eventErr = usb_host_lib_handle_events(pdMS_TO_TICKS(10), &eventFlags);
    if (eventErr != ESP_OK && eventErr != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_lib_handle_events(uninstall) failed: %s", esp_err_to_name(eventErr));
      setLastError(eventErr);
      return false;
    }
    uninstallErr = usb_host_uninstall();
  }
  if (uninstallErr != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_uninstall() failed: %s", esp_err_to_name(uninstallErr));
    setLastError(uninstallErr);
    return false;
  }
  return true;
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
  releaseAudioFeedbackTransfer(device);
}

uint32_t EspUsbHost::audioOutputPacingRate(const DeviceState &device) const
{
  return device.audioOutFeedbackRate != 0 ? device.audioOutFeedbackRate : device.audioSampleRate;
}

bool EspUsbHost::startAudioFeedback(DeviceState &device)
{
  device.audioOutFeedbackRate = 0;
  device.audioOutFeedbackUpdates = 0;
  device.audioOutFeedbackRejects = 0;

  if (device.audioOutFeedbackEndpointAddress == 0 ||
      device.audioOutFeedbackPacketSize == 0 ||
      device.audioOutFeedbackInterfaceNumber != device.audioOutInterfaceNumber)
  {
    // Synchronous or adaptive playback interface: no rate to follow.
    return true;
  }
  if (device.audioOutFeedbackTransfer)
  {
    return true;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(device.audioOutFeedbackPacketSize, 1, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(audio feedback) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = device.audioOutFeedbackEndpointAddress;
  transfer->callback = audioFeedbackTransferCallback;
  transfer->context = this;
  device.audioOutFeedbackTransfer = transfer;

  if (!submitAudioFeedbackTransfer(device))
  {
    releaseAudioFeedbackTransfer(device);
    return false;
  }
  ESP_LOGI(TAG, "USB Audio feedback polling started: ep=0x%02x size=%u nominal=%lu",
           device.audioOutFeedbackEndpointAddress,
           device.audioOutFeedbackPacketSize,
           static_cast<unsigned long>(device.audioSampleRate));
  return true;
}

bool EspUsbHost::submitAudioFeedbackTransfer(DeviceState &device)
{
  usb_transfer_t *transfer = device.audioOutFeedbackTransfer;
  if (!transfer)
  {
    return false;
  }

  transfer->num_bytes = device.audioOutFeedbackPacketSize;
  transfer->isoc_packet_desc[0].num_bytes = device.audioOutFeedbackPacketSize;
  transfer->isoc_packet_desc[0].actual_num_bytes = 0;
  transfer->isoc_packet_desc[0].status = USB_TRANSFER_STATUS_COMPLETED;

  esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(audio feedback) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }
  return true;
}

void EspUsbHost::applyAudioFeedback(DeviceState &device, const usb_transfer_t *transfer)
{
  const size_t length = static_cast<size_t>(transfer->isoc_packet_desc[0].actual_num_bytes);
  if (length == 0)
  {
    // The device had nothing to report in this interval. Keep the current rate.
    return;
  }

  const uint32_t feedbackQ16 = espUsbHostAudioDecodeFeedbackQ16(transfer->data_buffer, length);
  const bool highSpeed = device.info.speed == USB_SPEED_HIGH;
  const uint32_t rate = espUsbHostAudioFeedbackSampleRate(feedbackQ16, highSpeed);
  if (!espUsbHostAudioFeedbackRatePlausible(rate, device.audioSampleRate))
  {
    device.audioOutFeedbackRejects++;
    ESP_LOGD(TAG, "USB Audio feedback out of range: rate=%lu nominal=%lu len=%u",
             static_cast<unsigned long>(rate),
             static_cast<unsigned long>(device.audioSampleRate),
             static_cast<unsigned>(length));
    return;
  }

  device.audioOutFeedbackRate = rate;
  device.audioOutFeedbackUpdates++;
}

void EspUsbHost::releaseAudioFeedbackTransfer(DeviceState &device)
{
  usb_transfer_t *transfer = device.audioOutFeedbackTransfer;
  device.audioOutFeedbackTransfer = nullptr;
  device.audioOutFeedbackRate = 0;
  if (transfer)
  {
    usb_host_transfer_free(transfer);
  }
}

void EspUsbHost::audioFeedbackTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  DeviceState *device = host ? host->findDeviceByHandle(transfer->device_handle) : nullptr;
  if (!host || !device || device->audioOutFeedbackTransfer != transfer)
  {
    usb_host_transfer_free(transfer);
    return;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    host->applyAudioFeedback(*device, transfer);
    if (device->audioOutRunning && host->running_ && host->submitAudioFeedbackTransfer(*device))
    {
      return;
    }
  }
  else
  {
    ESP_LOGD(TAG, "audio feedback transfer status=%d ep=0x%02x",
             transfer->status,
             transfer->bEndpointAddress);
  }

  // Polling stopped: fall back to the negotiated rate instead of pacing playback
  // from a value that is no longer refreshed.
  device->audioOutFeedbackTransfer = nullptr;
  device->audioOutFeedbackRate = 0;
  usb_host_transfer_free(transfer);
}

void EspUsbHost::releaseEndpoints(DeviceState &device, bool clearEndpoints)
{
  releaseAudioOutputTransfers(device);
  releaseVendorOutQueue(device);
  releaseSerialOutQueue(device);
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
    resetEndpointState(endpoint);
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

bool EspUsbHost::finalizeDisconnectedDevice(DeviceState &device)
{
  if (!device.inUse || !device.disconnectPending)
  {
    return true;
  }

  // A DEV_GONE event can be delivered in the same client event pass as the
  // canceled endpoint callbacks. ESP-IDF may temporarily refuse to release an
  // interface until those callbacks have been fully dispatched. Keep the
  // handle and claimed-interface numbers so the next client loop can retry;
  // dropping them here leaves the gone address permanently open.
  uint8_t remaining = 0;
  for (uint8_t i = 0; i < device.interfaceCount; i++)
  {
    const uint8_t interfaceNumber = device.interfaces[i];
    const esp_err_t err = usb_host_interface_release(clientHandle_, device.handle, interfaceNumber);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
    {
      device.interfaces[remaining++] = interfaceNumber;
    }
  }
  for (uint8_t i = remaining; i < device.interfaceCount; i++)
  {
    device.interfaces[i] = 0;
  }
  device.interfaceCount = remaining;
  if (remaining != 0)
  {
    return false;
  }
  device.endpointChannelCount = 0;

  const esp_err_t closeErr = device.handle
                                 ? usb_host_device_close(clientHandle_, device.handle)
                                 : ESP_OK;
  if (closeErr != ESP_OK && closeErr != ESP_ERR_NOT_FOUND)
  {
    return false;
  }

  resetDeviceState(device);
  return true;
}

void EspUsbHost::clearParsedDescriptorState(DeviceState &device)
{
  device.hasKeyboardInterface = false;
  device.keyboardBitmapReport = false;
  device.keyboardLayoutInterface = 0xff;
  device.keyboardLayoutReportId = 0;
  device.keyboardHasModifierField = false;
  device.keyboardModifierBitOffset = 0;
  device.keyboardBitmapBitOffset = 0;
  device.keyboardBitmapBitCount = 0;
  device.keyboardBitmapUsageMin = 0;
  device.mouseLayout = EspUsbHostMouseReportLayout();
  device.mouseLayoutInterface = 0xff;
  device.hasKeyboardLedOutput = false;
  device.keyboardLedInterface = 0xff;
  device.keyboardLedReportId = 0;
  device.hasVendorInterface = false;
  device.hasVendorOutEndpoint = false;
  device.hasCdcControlInterface = false;
  device.hasCdcDataInterface = false;
  device.cdcConfigured = false;
  device.hasSerialOutEndpoint = false;
  device.hasVendorSerialInterface = false;
  device.hasUsbVendorInterface = false;
  device.hasUsbVendorInEndpoint = false;
  device.hasUsbVendorOutEndpoint = false;
  device.hasMidiInterface = false;
  device.hasMidiOutEndpoint = false;
  device.hasAudioInterface = false;
  device.hasAudioInEndpoint = false;
  device.hasAudioOutEndpoint = false;
  device.audioOutRunning = false;
  device.audioFeatureUnitCount = 0;
  device.audioProtocol = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
  device.audioClockSourceCount = 0;
  device.audioTerminalClockCount = 0;
  device.hasMscInterface = false;
  device.hasMscInEndpoint = false;
  device.hasMscOutEndpoint = false;
  device.hasNetworkInterface = false;
  device.networkInterface = EspUsbHostNetworkInterfaceInfo();
  releaseCcidInterface(device);
  device.ccidHasClassDescriptor = false;
  device.ccidDescriptorInterfaceNumber = 0xff;
  device.ccidInterfaceNumber = 0xff;
  device.ccidSlotCount = 1;
  device.ccidMaxMessageLength = 0;
  device.audioStreamInfoCount = 0;
  device.interfaceInfoCount = 0;
  device.endpointInfoCount = 0;
  device.hidReportDescriptorCount = 0;
  device.hidInputFieldCount = 0;
}

void EspUsbHost::releaseNetworkInterface(DeviceState &device)
{
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  networkStopNetif(device);
#endif

  // Stop new sends (the re-check in networkSendFrameInternal reads this), then
  // wait out any in-flight send before freeing the interface's resources.
  const bool hadInterface = device.hasNetworkInterface;
  device.hasNetworkInterface = false;
  device.networkLinkUp = false;
  networkDrainTx(device);

  if (!hadInterface || !clientHandle_ || !device.handle)
  {
    free(device.networkRxRing);
    free(device.networkAsm);
    device.networkRxRing = nullptr;
    device.networkAsm = nullptr;
    device.networkNtbInSize = 0;
    device.networkNtbOutMax = 0;
    device.networkAsmLen = 0;
    device.networkAsmExpected = 0;
    device.networkInterface = EspUsbHostNetworkInterfaceInfo();
    return;
  }

  // Free the bulk IN / notification IN transfers this interface started.
  const uint8_t inEndpoint = device.networkInterface.inEndpoint;
  const uint8_t notifyEndpoint = device.networkInterface.notificationEndpoint;
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse || endpoint.deviceHandle != device.handle ||
        (endpoint.address != inEndpoint && endpoint.address != notifyEndpoint))
    {
      continue;
    }
    if (endpoint.transfer)
    {
      usb_host_endpoint_clear(device.handle, endpoint.address); // halt + flush queued transfer
      usb_host_transfer_free(endpoint.transfer);
    }
    resetEndpointState(endpoint);
  }

  const uint8_t controlInterface = device.networkInterface.controlInterfaceNumber;
  const uint8_t dataInterface = device.networkInterface.dataInterfaceNumber;
  for (uint8_t i = 0; i < device.interfaceCount;)
  {
    const uint8_t interfaceNumber = device.interfaces[i];
    if (interfaceNumber == controlInterface || interfaceNumber == dataInterface)
    {
      usb_host_interface_release(clientHandle_, device.handle, interfaceNumber);
      for (uint8_t j = i; j + 1 < device.interfaceCount; j++)
      {
        device.interfaces[j] = device.interfaces[j + 1];
      }
      device.interfaces[device.interfaceCount - 1] = 0;
      device.interfaceCount--;
      continue;
    }
    i++;
  }

  for (uint8_t i = 0; i < device.interfaceInfoCount; i++)
  {
    EspUsbHostInterfaceInfo &info = device.interfaceInfos[i];
    if (info.number == controlInterface || info.number == dataInterface)
    {
      info.claimed = false;
    }
  }

  uint8_t channelCount = 2;
  if (device.networkInterface.notificationEndpoint)
  {
    channelCount++;
  }
  device.endpointChannelCount = device.endpointChannelCount >= channelCount
                                    ? static_cast<uint8_t>(device.endpointChannelCount - channelCount)
                                    : 0;
  device.hasNetworkInterface = false;
  device.networkInterface = EspUsbHostNetworkInterfaceInfo();
  device.networkLinkUp = false;
  // The IN transfers have been halted and freed above, so their callbacks can
  // no longer access the receive buffers.
  free(device.networkRxRing);
  free(device.networkAsm);
  device.networkRxRing = nullptr;
  device.networkAsm = nullptr;
  // The buffer is gone, so the size it was negotiated at must not survive: a
  // reopen renegotiates it (the device may even be a different one).
  device.networkNtbInSize = 0;
  device.networkNtbOutMax = 0;
  device.networkAsmLen = 0;
  device.networkAsmExpected = 0;
}

// Decide how large a device->host NTB may be, and make the device agree.
//
// Without this the host would just hope the device stays under its compile-time
// buffer: NCM leaves dwNtbInMaxSize entirely to the device unless the host
// lowers it with SET_NTB_INPUT_SIZE, and a device that batches several datagrams
// into one NTB (which they do once traffic picks up) then produces NTBs the host
// cannot read at all. The size is also rounded down to a multiple of the IN
// endpoint's max packet size because ESP-IDF documents IN transfer lengths as
// integer multiples of MPS.
uint16_t EspUsbHost::negotiateNetworkNtbInput(DeviceState &device, const EspUsbHostNetworkInterfaceInfo &network)
{
  const uint16_t mps = network.inMaxPacketSize ? network.inMaxPacketSize : 64;
  auto alignDown = [mps](size_t value) -> size_t
  {
    return (value / mps) * mps;
  };
  auto alignUp = [mps](size_t value) -> size_t
  {
    return ((value + mps - 1) / mps) * mps;
  };

  // What we would like to use: the compile-time default, MPS aligned. A device
  // whose MPS exceeds the default (not a real case today, but cheap to guard)
  // still gets one whole packet.
  size_t preferred = alignDown(ESP_USB_HOST_NETWORK_NTB_IN_MAX);
  if (preferred < mps)
  {
    preferred = mps;
  }
  device.networkNtbOutMax = 0;

  // ECM has no NTB layer at all, and a device that does not implement the NCM
  // functional descriptor cannot be asked about NTB parameters either.
  if (network.protocol != ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM)
  {
    return static_cast<uint16_t>(preferred);
  }

  uint8_t params[USB_CDC_NCM_NTB_PARAM_LEN] = {0};
  size_t actual = 0;
  const bool haveParams = submitVendorControl(device,
                                              0xa1, // IN | class | interface
                                              USB_CDC_REQ_GET_NTB_PARAMETERS,
                                              0,
                                              network.controlInterfaceNumber,
                                              params,
                                              sizeof(params),
                                              &actual,
                                              1000) &&
                          actual >= USB_CDC_NCM_NTB_PARAM_LEN;
  if (!haveParams)
  {
    // Not fatal: fall back to the default and let the oversized-NTB counter
    // report it if the device turns out to send more than that.
    ESP_LOGW(TAG, "network: GET_NTB_PARAMETERS failed (iface=%u); assuming %u byte NTB IN",
             network.controlInterfaceNumber,
             static_cast<unsigned>(preferred));
    return static_cast<uint16_t>(preferred);
  }

  const uint32_t deviceInMax = ncmRead32(params + 4);
  const uint32_t deviceOutMax = ncmRead32(params + 16);
  device.networkNtbOutMax = deviceOutMax > 0xffff ? 0xffff : static_cast<uint16_t>(deviceOutMax);
  ESP_LOGI(TAG, "network NTB parameters: dwNtbInMaxSize=%lu dwNtbOutMaxSize=%lu caps=0x%02x mps=%u",
           static_cast<unsigned long>(deviceInMax),
           static_cast<unsigned long>(deviceOutMax),
           network.networkCapabilities,
           mps);

  // Our host->device NTB is one datagram wide, so it only breaks if a device
  // advertises less than a full Ethernet frame plus framing.
  const size_t ourOutNtb = ncmAlign4(ESP_USB_HOST_NCM_NTH16_LEN + ESP_USB_HOST_NCM_NDP16_MIN_LEN) +
                           ESP_USB_HOST_NETWORK_MAX_FRAME;
  if (deviceOutMax != 0 && deviceOutMax < ourOutNtb)
  {
    ESP_LOGW(TAG, "network: device accepts only %lu byte NTBs OUT, we may send up to %u",
             static_cast<unsigned long>(deviceOutMax),
             static_cast<unsigned>(ourOutNtb));
  }

  if (deviceInMax <= preferred)
  {
    // The device already stays within our buffer; nothing to negotiate. Keep the
    // buffer at the device's maximum when that is smaller, still MPS aligned.
    size_t size = alignUp(deviceInMax);
    if (size == 0 || size > preferred)
    {
      size = preferred;
    }
    return static_cast<uint16_t>(size);
  }

  if (network.networkCapabilities & USB_CDC_NCM_CAP_NTB_INPUT_SIZE)
  {
    uint8_t payload[4];
    ncmWrite32(payload, static_cast<uint32_t>(preferred));
    size_t written = 0;
    if (submitVendorControl(device,
                            0x21, // OUT | class | interface
                            USB_CDC_REQ_SET_NTB_INPUT_SIZE,
                            0,
                            network.controlInterfaceNumber,
                            payload,
                            sizeof(payload),
                            &written,
                            1000))
    {
      ESP_LOGI(TAG, "network: SET_NTB_INPUT_SIZE(%u) accepted", static_cast<unsigned>(preferred));
      return static_cast<uint16_t>(preferred);
    }
    ESP_LOGW(TAG, "network: SET_NTB_INPUT_SIZE(%u) failed; following the device maximum instead",
             static_cast<unsigned>(preferred));
  }

  // The device keeps its own maximum, so the buffer has to follow it or every
  // batched NTB is unreadable. Pay the DMA memory up to the configured ceiling.
  size_t size = alignUp(deviceInMax);
  if (size > ESP_USB_HOST_NETWORK_NTB_IN_LIMIT)
  {
    size = alignDown(ESP_USB_HOST_NETWORK_NTB_IN_LIMIT);
    ESP_LOGW(TAG, "network: device may send %lu byte NTBs, capping the receive buffer at %u; "
                  "larger NTBs will be dropped (rxOversized)",
             static_cast<unsigned long>(deviceInMax),
             static_cast<unsigned>(size));
  }
  if (size < preferred)
  {
    size = preferred;
  }
  return static_cast<uint16_t>(size);
}

bool EspUsbHost::claimNetworkInterface(DeviceState &device, const EspUsbHostNetworkInterfaceInfo &network)
{
  if (!clientHandle_ || !device.handle || !network.complete())
  {
    return false;
  }

  // Agree on the device->host NTB size before allocating the buffer it sizes.
  // This runs on ep0, so it does not need the interfaces to be claimed yet, and
  // it precedes the data interface's SET_INTERFACE the same way a Linux host
  // orders the two. It also waits for control transfer completions, which are
  // dispatched by the USB client task -- safe only because the sole path here is
  // networkOpen(), which refuses to run from that task.
  const uint16_t ntbInSize = negotiateNetworkNtbInput(device, network);

  // Network receive storage is intentionally lazy: serial/HID/audio-only
  // sketches should not pay ~7 KB for every available device slot. Allocate
  // before claiming interfaces so failure leaves the USB device untouched.
  device.networkRxRing = static_cast<uint8_t *>(malloc(ESP_USB_HOST_NETWORK_RX_RING_SIZE));
  device.networkAsm = static_cast<uint8_t *>(malloc(ntbInSize));
  if (!device.networkRxRing || !device.networkAsm)
  {
    free(device.networkRxRing);
    free(device.networkAsm);
    device.networkRxRing = nullptr;
    device.networkAsm = nullptr;
    ESP_LOGE(TAG, "USB Network receive buffer allocation failed");
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  auto markClaim = [&](uint8_t interfaceNumber, uint8_t alternate, esp_err_t result, bool claimed)
  {
    for (uint8_t i = 0; i < device.interfaceInfoCount; i++)
    {
      EspUsbHostInterfaceInfo &info = device.interfaceInfos[i];
      if (info.number == interfaceNumber && info.alternate == alternate)
      {
        info.claimAttempted = true;
        info.claimResult = result;
        info.claimed = claimed;
      }
    }
  };

  esp_err_t err = usb_host_interface_claim(clientHandle_,
                                           device.handle,
                                           network.controlInterfaceNumber,
                                           network.controlInterfaceAlternate);
  markClaim(network.controlInterfaceNumber, network.controlInterfaceAlternate, err, err == ESP_OK);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_interface_claim(network control iface=%u alt=%u) failed: %s",
             network.controlInterfaceNumber,
             network.controlInterfaceAlternate,
             esp_err_to_name(err));
    setLastError(err);
    free(device.networkRxRing);
    free(device.networkAsm);
    device.networkRxRing = nullptr;
    device.networkAsm = nullptr;
    return false;
  }

  err = usb_host_interface_claim(clientHandle_,
                                 device.handle,
                                 network.dataInterfaceNumber,
                                 network.dataInterfaceAlternate);
  markClaim(network.dataInterfaceNumber, network.dataInterfaceAlternate, err, err == ESP_OK);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_interface_claim(network data iface=%u alt=%u) failed: %s",
             network.dataInterfaceNumber,
             network.dataInterfaceAlternate,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_interface_release(clientHandle_, device.handle, network.controlInterfaceNumber);
    markClaim(network.controlInterfaceNumber, network.controlInterfaceAlternate, ESP_OK, false);
    free(device.networkRxRing);
    free(device.networkAsm);
    device.networkRxRing = nullptr;
    device.networkAsm = nullptr;
    return false;
  }

  if (network.dataInterfaceAlternate > 0)
  {
    submitSetInterface(device, network.dataInterfaceNumber, network.dataInterfaceAlternate);
  }

  if (device.interfaceCount + 2 <= sizeof(device.interfaces))
  {
    device.interfaces[device.interfaceCount++] = network.controlInterfaceNumber;
    device.interfaces[device.interfaceCount++] = network.dataInterfaceNumber;
  }

  uint8_t channelCount = 2;
  if (network.notificationEndpoint)
  {
    channelCount++;
  }
  device.endpointChannelCount = static_cast<uint8_t>(device.endpointChannelCount + channelCount);
  device.networkInterface = network;
  device.hasNetworkInterface = true;
  device.networkRxHead = 0;
  device.networkRxTail = 0;
  device.networkTxSequence = 0;
  device.networkLinkUp = false;
  // TX lock / completion semaphore are created once per device slot and kept for
  // the slot's lifetime (see DeviceState); (re)create only if this slot never had
  // a network interface before.
  if (!device.networkTxLock)
  {
    device.networkTxLock = xSemaphoreCreateMutex();
  }
  if (!device.networkOutDone)
  {
    device.networkOutDone = xSemaphoreCreateBinary();
  }
  device.networkAsmLen = 0;
  device.networkAsmExpected = 0;
  device.networkNtbInSize = ntbInSize;
  device.networkRxNtbCount = 0;
  device.networkRxFrameCount = 0;
  device.networkTxCount = 0;
  device.networkTxFailCount = 0;
  device.networkRxOversizedCount = 0;

  // Start the bulk IN (NTB receive) and, if present, the interrupt IN
  // (link-status notification) transfers. Failure here is not fatal to the
  // claim itself; frames just will not flow until it succeeds.
  startNetworkEndpoints(device);

  ESP_LOGI(TAG, "USB Network ready: address=%u config=%u protocol=%s control_iface=%u data_iface=%u alt=%u endpoints=%u ntb_in=%u",
           device.info.address,
           network.configurationValue,
           espUsbHostNetworkProtocolName(network.protocol),
           network.controlInterfaceNumber,
           network.dataInterfaceNumber,
           network.dataInterfaceAlternate,
           channelCount,
           device.networkNtbInSize);
  return true;
}

bool EspUsbHost::startNetworkEndpoints(DeviceState &device)
{
  if (!clientHandle_ || !device.handle || !device.hasNetworkInterface)
  {
    return false;
  }
  const EspUsbHostNetworkInterfaceInfo &network = device.networkInterface;
  bool ok = true;

  // Bulk IN: one transfer sized for a whole device->host NTB, using the size
  // negotiated in negotiateNetworkNtbInput(). The NTB spans many max-packet
  // reads, but a single submit of that length completes on the terminating short
  // packet, so one completion == one NTB.
  const uint16_t ntbInSize = device.networkNtbInSize ? device.networkNtbInSize
                                                    : static_cast<uint16_t>(ESP_USB_HOST_NETWORK_NTB_IN_MAX);
  if (network.inEndpoint && !findEndpoint(device.handle, network.inEndpoint))
  {
    EndpointState *endpoint = allocateEndpoint(device);
    if (endpoint)
    {
      esp_err_t err = usb_host_transfer_alloc(ntbInSize, 0, &endpoint->transfer);
      if (err == ESP_OK)
      {
        endpoint->address = network.inEndpoint;
        endpoint->interfaceNumber = network.dataInterfaceNumber;
        endpoint->alternate = network.dataInterfaceAlternate;
        endpoint->interfaceClass = USB_CLASS_CDC_DATA_VALUE;
        endpoint->transfer->device_handle = device.handle;
        endpoint->transfer->bEndpointAddress = network.inEndpoint;
        endpoint->transfer->callback = transferCallback;
        endpoint->transfer->context = this;
        endpoint->transfer->num_bytes = ntbInSize;
        if (!submitInputTransfer(*endpoint))
        {
          ok = false;
        }
      }
      else
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(network IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        ok = false;
      }
    }
    else
    {
      ESP_LOGW(TAG, "No endpoint slots available for network IN");
      ok = false;
    }
  }

  // Interrupt IN: link-status notifications (NETWORK_CONNECTION / SPEED_CHANGE).
  if (network.notificationEndpoint && network.notificationMaxPacketSize &&
      !findEndpoint(device.handle, network.notificationEndpoint))
  {
    EndpointState *endpoint = allocateEndpoint(device);
    if (endpoint)
    {
      esp_err_t err = usb_host_transfer_alloc(network.notificationMaxPacketSize, 0, &endpoint->transfer);
      if (err == ESP_OK)
      {
        endpoint->address = network.notificationEndpoint;
        endpoint->interfaceNumber = network.controlInterfaceNumber;
        endpoint->alternate = network.controlInterfaceAlternate;
        endpoint->interfaceClass = USB_CLASS_CDC_CONTROL_VALUE;
        endpoint->transfer->device_handle = device.handle;
        endpoint->transfer->bEndpointAddress = network.notificationEndpoint;
        endpoint->transfer->callback = transferCallback;
        endpoint->transfer->context = this;
        endpoint->transfer->num_bytes = network.notificationMaxPacketSize;
        submitInputTransfer(*endpoint); // notifications are optional; ignore failure
      }
      else
      {
        endpoint->inUse = false;
        ESP_LOGD(TAG, "usb_host_transfer_alloc(network notify) failed: %s", esp_err_to_name(err));
      }
    }
  }

  return ok;
}

// Parse one received NCM NTB (device->host) and deliver each contained Ethernet
// datagram. NCM 1.0, 16-bit format: NTH16 header, then one or more NDP16 tables
// chained by wNextNdpIndex, each listing (offset,length) datagram entries.
// Bulk-IN completion handler. A device->host NTB can arrive across several
// completions (one per USB packet at full speed), so reassemble by wBlockLength
// before parsing.
void EspUsbHost::handleNetworkInput(DeviceState &device, EndpointState &endpoint, const uint8_t *data, size_t length)
{
  (void)endpoint;
  if (!data || length == 0)
  {
    return;
  }

  // Resync: if a fresh chunk starts with an NTH16 signature while a previous NTB
  // is still being assembled, the tail of that NTB was lost (e.g. a dropped/errored
  // bulk-IN completion). Discard the stale partial and start over on this chunk
  // rather than appending it as continuation data and parsing a corrupt NTB.
  if (device.networkAsmLen != 0 && length >= ESP_USB_HOST_NCM_NTH16_LEN &&
      ncmRead32(data) == ESP_USB_HOST_NCM_NTH16_SIG)
  {
    ESP_LOGD(TAG, "network RX: resync on new NTH16 (dropped %u of %u bytes)",
             static_cast<unsigned>(device.networkAsmLen), static_cast<unsigned>(device.networkAsmExpected));
    device.networkAsmLen = 0;
    device.networkAsmExpected = 0;
  }

  const uint16_t ntbInSize = device.networkNtbInSize ? device.networkNtbInSize
                                                    : static_cast<uint16_t>(ESP_USB_HOST_NETWORK_NTB_IN_MAX);

  // Start of a new NTB: the chunk must begin with an NTH16 header.
  if (device.networkAsmLen == 0)
  {
    if (length < ESP_USB_HOST_NCM_NTH16_LEN || ncmRead32(data) != ESP_USB_HOST_NCM_NTH16_SIG)
    {
      ESP_LOGD(TAG, "network RX: stray chunk (no NTH16), len=%u", static_cast<unsigned>(length));
      return;
    }
    const uint16_t headerLength = ncmRead16(data + 4);
    const uint16_t blockLength = ncmRead16(data + 8);
    if (headerLength < ESP_USB_HOST_NCM_NTH16_LEN ||
        blockLength < ESP_USB_HOST_NCM_NTH16_LEN)
    {
      return;
    }
    if (blockLength > ntbInSize)
    {
      // The device is sending larger NTBs than it agreed to (or than we could
      // negotiate). Every datagram in this NTB is lost, so make it visible
      // instead of failing as unexplained packet loss: the first occurrence
      // warns, the rest only bump the counter reported by networkStats().
      device.networkRxOversizedCount++;
      if (device.networkRxOversizedCount == 1)
      {
        ESP_LOGW(TAG, "network RX: NTB of %u bytes exceeds the negotiated %u; dropping it "
                      "(device ignores SET_NTB_INPUT_SIZE - raise ESP_USB_HOST_NETWORK_NTB_IN_LIMIT)",
                 blockLength,
                 ntbInSize);
      }
      else
      {
        ESP_LOGD(TAG, "network RX: oversized NTB (%u > %u), dropped %lu so far",
                 blockLength,
                 ntbInSize,
                 static_cast<unsigned long>(device.networkRxOversizedCount));
      }
      return;
    }
    device.networkAsmExpected = blockLength;
    device.networkRxNtbCount++;
  }

  // Append this chunk to the reassembly buffer.
  size_t copy = length;
  if (device.networkAsmLen + copy > ntbInSize)
  {
    copy = ntbInSize - device.networkAsmLen;
  }
  memcpy(device.networkAsm + device.networkAsmLen, data, copy);
  device.networkAsmLen = static_cast<uint16_t>(device.networkAsmLen + copy);

  if (device.networkAsmLen >= device.networkAsmExpected)
  {
    parseNetworkNtb(device, device.networkAsm, device.networkAsmExpected);
    device.networkAsmLen = 0;
    device.networkAsmExpected = 0;
  }
}

// Parse a fully-assembled device->host NTB (NCM 1.0, 16-bit) and deliver each
// contained Ethernet datagram.
void EspUsbHost::parseNetworkNtb(DeviceState &device, const uint8_t *data, size_t length)
{
  if (!data || length < ESP_USB_HOST_NCM_NTH16_LEN || ncmRead32(data) != ESP_USB_HOST_NCM_NTH16_SIG)
  {
    return;
  }
  // NTH16: dwSignature(0..4) wHeaderLength(4..6) wSequence(6..8)
  //        wBlockLength(8..10) wNdpIndex(10..12)
  uint16_t blockLength = ncmRead16(data + 8);
  uint16_t ndpIndex = ncmRead16(data + 10);
  if (blockLength == 0 || blockLength > length)
  {
    blockLength = static_cast<uint16_t>(length);
  }

  uint8_t ndpVisits = 0;
  while (ndpIndex != 0 && ndpVisits < 8)
  {
    ndpVisits++;
    if (ndpIndex + 8 > blockLength)
    {
      break;
    }
    const uint8_t *ndp = data + ndpIndex;
    if (ncmRead32(ndp) != ESP_USB_HOST_NCM_NDP16_SIG)
    {
      // Some devices use "NCM1" for the second NDP; accept any 0x314D434E too.
      if (ncmRead32(ndp) != 0x314D434E)
      {
        break;
      }
    }
    const uint16_t ndpLength = ncmRead16(ndp + 4);
    const uint16_t nextNdpIndex = ncmRead16(ndp + 6);
    if (ndpLength < 8 || ndpIndex + ndpLength > blockLength)
    {
      break;
    }
    // Datagram pointer table: pairs of (wDatagramIndex, wDatagramLength) at
    // offset 8, terminated by a (0,0) entry.
    for (uint16_t off = 8; off + 4 <= ndpLength; off += 4)
    {
      const uint16_t dgIndex = ncmRead16(ndp + off);
      const uint16_t dgLength = ncmRead16(ndp + off + 2);
      if (dgIndex == 0 || dgLength == 0)
      {
        break;
      }
      if (dgIndex + dgLength > blockLength)
      {
        continue;
      }
      deliverNetworkFrame(device, data + dgIndex, dgLength);
    }
    ndpIndex = nextNdpIndex;
  }
}

// Deliver one Ethernet frame extracted from an NTB: into lwIP if a netif is
// attached, otherwise to the raw callback and the poll ring.
void EspUsbHost::deliverNetworkFrame(DeviceState &device, const uint8_t *frame, size_t length)
{
  if (!frame || length == 0 || length > ESP_USB_HOST_NETWORK_MAX_FRAME)
  {
    return;
  }
  device.networkRxFrameCount++;

#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  if (device.networkNetifAttached && device.networkNetif)
  {
    // esp_netif owns the buffer until it calls the free hook; hand it a copy.
    uint8_t *buf = static_cast<uint8_t *>(malloc(length));
    if (buf)
    {
      memcpy(buf, frame, length);
      if (esp_netif_receive(static_cast<esp_netif_t *>(device.networkNetif), buf, length, buf) != ESP_OK)
      {
        free(buf);
      }
    }
    return;
  }
#endif

  if (networkFrameCallback_)
  {
    EspUsbHostNetworkFrame event;
    event.address = device.info.address;
    event.protocol = device.networkInterface.protocol;
    event.data = frame;
    event.length = length;
    networkFrameCallback_(event);
  }

  // Push into the poll ring as [uint16 len][payload]. This is the producer side
  // of a single-producer (USB task) / single-consumer (networkReadFrame caller)
  // ring: only the consumer moves the tail, so on overflow we drop the *new*
  // frame rather than touch the tail here.
  const size_t needed = 2 + length;
  const size_t tail = device.networkRxTail;
  const size_t free = (tail + ESP_USB_HOST_NETWORK_RX_RING_SIZE - 1 - device.networkRxHead) % ESP_USB_HOST_NETWORK_RX_RING_SIZE;
  if (needed > free)
  {
    ESP_LOGD(TAG, "network RX ring full, dropping frame (%u bytes)", static_cast<unsigned>(length));
    return;
  }
  uint8_t lenBytes[2];
  ncmWrite16(lenBytes, static_cast<uint16_t>(length));
  device.networkRxRing[device.networkRxHead] = lenBytes[0];
  device.networkRxRing[(device.networkRxHead + 1) % ESP_USB_HOST_NETWORK_RX_RING_SIZE] = lenBytes[1];
  for (size_t i = 0; i < length; i++)
  {
    device.networkRxRing[(device.networkRxHead + 2 + i) % ESP_USB_HOST_NETWORK_RX_RING_SIZE] = frame[i];
  }
  device.networkRxHead = static_cast<uint16_t>((device.networkRxHead + needed) % ESP_USB_HOST_NETWORK_RX_RING_SIZE);
}

// CDC notification (interrupt IN): track link up/down for networkLinkUp().
void EspUsbHost::handleNetworkNotification(DeviceState &device, const uint8_t *data, size_t length)
{
  if (!data || length < 8)
  {
    return;
  }
  const uint8_t bNotification = data[1];
  const uint16_t wValue = ncmRead16(data + 2);
  if (bNotification == ESP_USB_HOST_CDC_NOTIFY_NETWORK_CONNECTION)
  {
    device.networkLinkUp = (wValue != 0);
    ESP_LOGI(TAG, "network link %s (address=%u)", device.networkLinkUp ? "up" : "down", device.info.address);
  }
  else if (bNotification == ESP_USB_HOST_CDC_NOTIFY_SPEED_CHANGE)
  {
    ESP_LOGD(TAG, "network connection speed change (address=%u)", device.info.address);
  }
}

// Build a single-datagram NCM NTB (16-bit) around one Ethernet frame.
// Layout: NTH16(12) | NDP16(16: 8 header + 2 datagram-table entries) | frame,
// all 4-byte aligned. Returns the total NTB length, or 0 on overflow.
size_t EspUsbHost::buildNcmFrame(uint8_t *out, size_t outCapacity, const uint8_t *frame, size_t length, uint16_t sequence)
{
  const size_t ndpOffset = ESP_USB_HOST_NCM_NTH16_LEN;      // 12
  const size_t ndpLength = ESP_USB_HOST_NCM_NDP16_MIN_LEN;  // 16
  const size_t dgOffset = ncmAlign4(ndpOffset + ndpLength); // 28
  const size_t total = dgOffset + length;
  if (!out || !frame || length == 0 || total > outCapacity || total > 0xffff)
  {
    return 0;
  }
  memset(out, 0, dgOffset);

  // NTH16
  ncmWrite32(out + 0, ESP_USB_HOST_NCM_NTH16_SIG);
  ncmWrite16(out + 4, ESP_USB_HOST_NCM_NTH16_LEN);
  ncmWrite16(out + 6, sequence);
  ncmWrite16(out + 8, static_cast<uint16_t>(total)); // wBlockLength
  ncmWrite16(out + 10, static_cast<uint16_t>(ndpOffset));

  // NDP16: dwSignature(4) | wLength(2) | wNextNdpIndex(2) | datagram entries
  ncmWrite32(out + ndpOffset + 0, ESP_USB_HOST_NCM_NDP16_SIG);
  ncmWrite16(out + ndpOffset + 4, static_cast<uint16_t>(ndpLength));
  ncmWrite16(out + ndpOffset + 6, 0);                               // wNextNdpIndex (0 = last NDP)
  ncmWrite16(out + ndpOffset + 8, static_cast<uint16_t>(dgOffset)); // datagram[0].index
  ncmWrite16(out + ndpOffset + 10, static_cast<uint16_t>(length));  // datagram[0].length
  // datagram[1] entry at [12..16) stays zero -> null terminator

  memcpy(out + dgOffset, frame, length);
  return total;
}

// Synchronously send one Ethernet frame as an NTB over the network bulk OUT.
// Serializes concurrent callers (a user thread and the lwIP transmit hook) on
// networkTxLock, and re-checks the interface under the lock so a disconnect /
// networkClose() that ran while we waited for the lock cannot send on a freed
// device.
bool EspUsbHost::networkSendFrameInternal(DeviceState &device, const uint8_t *frame, size_t length)
{
  if (!device.hasNetworkInterface || !device.handle || !clientHandle_)
  {
    return false;
  }
  if (!device.networkInterface.outEndpoint)
  {
    return false;
  }
  if (!frame || length == 0 || length > ESP_USB_HOST_NETWORK_MAX_FRAME)
  {
    return false;
  }

  SemaphoreHandle_t lock = device.networkTxLock;
  if (!lock || !device.networkOutDone)
  {
    return false;
  }
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(1000)) != pdTRUE)
  {
    ESP_LOGW(TAG, "network TX busy ep=0x%02x", device.networkInterface.outEndpoint);
    setLastError(ESP_ERR_TIMEOUT);
    device.networkTxFailCount++;
    return false;
  }

  bool ok = false;
  if (device.hasNetworkInterface && device.handle && clientHandle_ &&
      device.networkInterface.outEndpoint && device.networkOutDone)
  {
    ok = networkSendLocked(device, frame, length);
  }
  xSemaphoreGive(lock);
  return ok;
}

// Sends one frame with networkTxLock held. Owns the reusable OUT transfer and
// completion semaphore stored on the device.
bool EspUsbHost::networkSendLocked(DeviceState &device, const uint8_t *frame, size_t length)
{
  const EspUsbHostNetworkInterfaceInfo &network = device.networkInterface;
  const size_t ntbCapacity = ncmAlign4(ESP_USB_HOST_NCM_NTH16_LEN + ESP_USB_HOST_NCM_NDP16_MIN_LEN) + ESP_USB_HOST_NETWORK_MAX_FRAME;

  if (!device.networkOutTransfer)
  {
    esp_err_t err = usb_host_transfer_alloc(ntbCapacity, 0, &device.networkOutTransfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_alloc(network OUT) failed: %s", esp_err_to_name(err));
      setLastError(err);
      device.networkOutTransfer = nullptr;
      device.networkTxFailCount++;
      return false;
    }
  }
  usb_transfer_t *transfer = device.networkOutTransfer;

  const size_t ntbLength = buildNcmFrame(transfer->data_buffer, ntbCapacity, frame, length, device.networkTxSequence++);
  if (ntbLength == 0)
  {
    device.networkTxFailCount++;
    return false;
  }

  EspUsbHostSyncTransferContext context;
  context.done = device.networkOutDone;
  xSemaphoreTake(device.networkOutDone, 0); // clear any stale completion

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = network.outEndpoint;
  transfer->callback = syncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = ntbLength;

  esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(network OUT ep=0x%02x) failed: %s", network.outEndpoint, esp_err_to_name(err));
    setLastError(err);
    device.networkTxFailCount++;
    return false;
  }

  bool done = xSemaphoreTake(device.networkOutDone, pdMS_TO_TICKS(250)) == pdTRUE;
  if (!done)
  {
    // The driver still owns the transfer. Flush the endpoint so the transfer is
    // canceled (its callback fires with CANCELED and signals the semaphore), then
    // reclaim ownership before returning so the transfer is safe to reuse and the
    // stack-allocated context is not referenced after we return.
    ESP_LOGW(TAG, "network bulk OUT timeout ep=0x%02x, flushing", network.outEndpoint);
    setLastError(ESP_ERR_TIMEOUT);
    if (device.handle)
    {
      usb_host_endpoint_clear(device.handle, network.outEndpoint);
    }
    xSemaphoreTake(device.networkOutDone, pdMS_TO_TICKS(500));
  }
  const bool ok = done && context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (ok)
  {
    device.networkTxCount++;
  }
  else
  {
    device.networkTxFailCount++;
  }
  return ok;
}

// Wait for any in-flight send to finish and free the reusable OUT transfer. The
// caller must clear device.hasNetworkInterface first so no new send proceeds
// past the re-check in networkSendFrameInternal. The TX lock / completion
// semaphore are intentionally kept alive and reused (never deleted here) so a
// concurrent sender can never block on or signal a freed handle.
void EspUsbHost::networkDrainTx(DeviceState &device)
{
  SemaphoreHandle_t lock = device.networkTxLock;
  bool held = true;
  if (lock)
  {
    // Worst case an in-flight send holds the lock ~750ms (250ms submit wait +
    // 500ms flush reclaim); 2s leaves margin.
    held = xSemaphoreTake(lock, pdMS_TO_TICKS(2000)) == pdTRUE;
  }
  // Only touch the transfer if we hold the lock (or there is no lock, in which
  // case no send can be in flight): freeing it under a wedged send would be a
  // use-after-free in the USB driver.
  if (held && device.networkOutTransfer)
  {
    usb_host_transfer_free(device.networkOutTransfer);
    device.networkOutTransfer = nullptr;
  }
  else if (!held)
  {
    ESP_LOGW(TAG, "network TX drain timed out; keeping OUT transfer to avoid UAF");
  }
  if (lock && held)
  {
    xSemaphoreGive(lock);
  }
}

bool EspUsbHost::networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address)
{
  DeviceState *device = findNetworkDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "networkWriteFrame() no open network interface");
    return false;
  }
  return networkSendFrameInternal(*device, frame, length);
}

size_t EspUsbHost::networkReadFrame(uint8_t *buffer, size_t length, uint8_t address)
{
  if (!buffer || length == 0)
  {
    return 0;
  }
  DeviceState *device = findNetworkDevice(address);
  if (!device)
  {
    return 0;
  }
  if (device->networkRxHead == device->networkRxTail)
  {
    return 0; // empty
  }
  uint8_t lenBytes[2];
  lenBytes[0] = device->networkRxRing[device->networkRxTail];
  lenBytes[1] = device->networkRxRing[(device->networkRxTail + 1) % ESP_USB_HOST_NETWORK_RX_RING_SIZE];
  const uint16_t frameLen = ncmRead16(lenBytes);
  const size_t copyLen = frameLen < length ? frameLen : length;
  for (size_t i = 0; i < copyLen; i++)
  {
    buffer[i] = device->networkRxRing[(device->networkRxTail + 2 + i) % ESP_USB_HOST_NETWORK_RX_RING_SIZE];
  }
  device->networkRxTail = static_cast<uint16_t>((device->networkRxTail + 2 + frameLen) % ESP_USB_HOST_NETWORK_RX_RING_SIZE);
  return copyLen;
}

bool EspUsbHost::networkLinkUp(uint8_t address) const
{
  const DeviceState *device = findNetworkDevice(address);
  return device && device->networkLinkUp;
}

bool EspUsbHost::networkStats(EspUsbHostNetworkStats &stats, uint8_t address) const
{
  stats = EspUsbHostNetworkStats();
  const DeviceState *device = findNetworkDevice(address);
  if (!device)
  {
    return false;
  }
  stats.ready = device->hasNetworkInterface;
  stats.linkUp = device->networkLinkUp;
  stats.netifAttached = device->networkNetifAttached;
  stats.rxNtb = device->networkRxNtbCount;
  stats.rxFrames = device->networkRxFrameCount;
  stats.txFrames = device->networkTxCount;
  stats.txFails = device->networkTxFailCount;
  stats.rxOversized = device->networkRxOversizedCount;
  stats.ntbInSize = device->networkNtbInSize;
  return true;
}

bool EspUsbHost::networkAttachNetif(const EspUsbHostNetworkConfig &config, uint8_t address)
{
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "networkAttachNetif() cannot run from USB client task");
    return false;
  }
  DeviceState *device = findNetworkDevice(address);
  if (!device)
  {
    // Not opened yet: open the first matching candidate on the target device.
    if (!networkOpen(address))
    {
      return false;
    }
    device = findNetworkDevice(address);
  }
  if (!device)
  {
    return false;
  }
  if (!device->networkNetifAttached)
  {
    // networkOpen() issues SET_INTERFACE(data, alt=1) asynchronously; give the
    // device time to activate its data endpoints before the DHCP client starts
    // exchanging frames, otherwise the first DHCP round can be dropped.
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  return networkStartNetif(*device, config);
#else
  (void)config;
  (void)address;
  ESP_LOGW(TAG, "networkAttachNetif() requires esp_netif (not available in this build)");
  setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
#endif
}

bool EspUsbHost::networkDetachNetif(uint8_t address)
{
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  DeviceState *device = findNetworkDevice(address);
  if (!device)
  {
    return false;
  }
  networkStopNetif(*device);
  return true;
#else
  (void)address;
  return false;
#endif
}

IPAddress EspUsbHost::networkLocalIP(uint8_t address) const
{
#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
  const DeviceState *device = findNetworkDevice(address);
  if (device && device->networkNetifAttached && device->networkNetif)
  {
    esp_netif_ip_info_t info = {};
    if (esp_netif_get_ip_info(static_cast<esp_netif_t *>(device->networkNetif), &info) == ESP_OK)
    {
      return IPAddress(info.ip.addr);
    }
  }
#else
  (void)address;
#endif
  return IPAddress(static_cast<uint32_t>(0));
}

#if defined(ESP_USB_HOST_HAS_ESP_NETIF)
bool EspUsbHost::readNetworkMac(DeviceState &device, uint8_t mac[6])
{
  const uint8_t strIndex = device.networkInterface.macAddressStringIndex;
  if (strIndex == 0 || !device.handle)
  {
    return false;
  }

  // GET_DESCRIPTOR(STRING, iMACAddress). String descriptors take a LANGID in
  // wIndex; try US English then 0 as a fallback.
  const uint16_t wValue = static_cast<uint16_t>((0x03 << 8) | strIndex);
  uint8_t buf[64] = {};
  size_t actual = 0;
  bool ok = submitVendorControl(device, 0x80, USB_REQUEST_GET_DESCRIPTOR, wValue, 0x0409,
                                buf, sizeof(buf), &actual, 1000);
  if (!ok || actual < 26 || buf[1] != 0x03)
  {
    memset(buf, 0, sizeof(buf));
    actual = 0;
    ok = submitVendorControl(device, 0x80, USB_REQUEST_GET_DESCRIPTOR, wValue, 0x0000,
                             buf, sizeof(buf), &actual, 1000);
  }
  // String descriptor: [bLength][bDescriptorType=0x03][UTF-16LE...]. The MAC is
  // 12 ASCII hex chars, so the low byte of each 16-bit unit is the character.
  if (!ok || actual < 26 || buf[1] != 0x03)
  {
    return false;
  }

  auto hexVal = [](uint8_t c) -> int
  {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return -1;
  };
  for (int i = 0; i < 6; i++)
  {
    const int hi = hexVal(buf[2 + 4 * i]);     // char (2i)   low byte
    const int lo = hexVal(buf[2 + 4 * i + 2]); // char (2i+1) low byte
    if (hi < 0 || lo < 0)
    {
      return false;
    }
    mac[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool EspUsbHost::networkStartNetif(DeviceState &device, const EspUsbHostNetworkConfig &config)
{
  if (device.networkNetifAttached)
  {
    return true;
  }
  if (!device.hasNetworkInterface)
  {
    return false;
  }

  // The lwIP stack and default event loop are process-wide; both are idempotent.
  esp_netif_init();
  esp_err_t evt = esp_event_loop_create_default();
  if (evt != ESP_OK && evt != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGW(TAG, "esp_event_loop_create_default() failed: %s", esp_err_to_name(evt));
    setLastError(evt);
    return false;
  }

  EspUsbHostNetifDriver *driver = static_cast<EspUsbHostNetifDriver *>(calloc(1, sizeof(EspUsbHostNetifDriver)));
  if (!driver)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }
  driver->host = this;
  driver->address = device.info.address;
  driver->base.post_attach = espUsbHostNetifPostAttach;

  esp_netif_inherent_config_t base = {};
  uint32_t flags = ESP_NETIF_FLAG_AUTOUP;
  esp_netif_ip_info_t ipInfo = {};
  if (config.dhcpClient)
  {
    flags |= ESP_NETIF_DHCP_CLIENT;
  }
  else
  {
    ipInfo.ip.addr = static_cast<uint32_t>(config.ip);
    ipInfo.gw.addr = static_cast<uint32_t>(config.gateway);
    ipInfo.netmask.addr = static_cast<uint32_t>(config.subnet);
    base.ip_info = &ipInfo;
  }
  base.flags = static_cast<esp_netif_flags_t>(flags);
  base.if_key = "USB_NCM";
  base.if_desc = "usbncm";
  base.route_prio = 15;

  esp_netif_driver_ifconfig_t driverCfg = {};
  driverCfg.handle = driver;
  driverCfg.transmit = espUsbHostNetifTransmit;
  driverCfg.driver_free_rx_buffer = espUsbHostNetifFreeRx;

  esp_netif_config_t cfg = {};
  cfg.base = &base;
  cfg.driver = &driverCfg;
  cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;

  esp_netif_t *netif = esp_netif_new(&cfg);
  if (!netif)
  {
    ESP_LOGW(TAG, "esp_netif_new(USB_NCM) failed");
    free(driver);
    setLastError(ESP_FAIL);
    return false;
  }

  if (esp_netif_attach(netif, driver) != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_netif_attach(USB_NCM) failed");
    esp_netif_destroy(netif); // A5 fix: no leak / key stays reusable
    free(driver);
    setLastError(ESP_FAIL);
    return false;
  }

  // Host interface MAC: use the adapter's advertised iMACAddress (what a real
  // USB NIC expects on the wire and what OS drivers use). Fall back to a
  // locally-administered address derived from the USB device address when the
  // device provides no MAC string.
  uint8_t mac[6];
  if (!readNetworkMac(device, mac))
  {
    mac[0] = 0x02;
    mac[1] = 0x55;
    mac[2] = 0x53;
    mac[3] = 0x42;
    mac[4] = device.info.address;
    mac[5] = 0x01;
    ESP_LOGW(TAG, "network: no iMACAddress string; using derived MAC");
  }
  esp_netif_set_mac(netif, mac);

  esp_netif_action_start(netif, nullptr, 0, nullptr);
  // Bring the link up first so the DHCP client sends DISCOVER on an up netif.
  esp_netif_action_connected(netif, nullptr, 0, nullptr);
  if (config.dhcpClient)
  {
    esp_netif_dhcpc_stop(netif); // may already be running from the start action
    esp_err_t derr = esp_netif_dhcpc_start(netif);
    if (derr != ESP_OK && derr != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
    {
      ESP_LOGW(TAG, "esp_netif_dhcpc_start() failed: %s", esp_err_to_name(derr));
    }
  }
  else
  {
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &ipInfo);
    if (static_cast<uint32_t>(config.dns1) != 0)
    {
      esp_netif_dns_info_t dns = {};
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(config.dns1);
      esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    if (static_cast<uint32_t>(config.dns2) != 0)
    {
      esp_netif_dns_info_t dns = {};
      dns.ip.type = ESP_IPADDR_TYPE_V4;
      dns.ip.u_addr.ip4.addr = static_cast<uint32_t>(config.dns2);
      esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns);
    }
  }

  device.networkNetif = netif;
  device.networkNetifAttached = true;
  ESP_LOGI(TAG, "USB network netif attached (address=%u dhcp=%u)", device.info.address, config.dhcpClient ? 1 : 0);
  return true;
}

void EspUsbHost::networkStopNetif(DeviceState &device)
{
  if (!device.networkNetifAttached || !device.networkNetif)
  {
    device.networkNetifAttached = false;
    device.networkNetif = nullptr;
    return;
  }
  esp_netif_t *netif = static_cast<esp_netif_t *>(device.networkNetif);
  EspUsbHostNetifDriver *driver = static_cast<EspUsbHostNetifDriver *>(esp_netif_get_io_driver(netif));
  esp_netif_action_disconnected(netif, nullptr, 0, nullptr);
  esp_netif_action_stop(netif, nullptr, 0, nullptr);
  esp_netif_destroy(netif);
  if (driver)
  {
    free(driver);
  }
  device.networkNetif = nullptr;
  device.networkNetifAttached = false;
  ESP_LOGI(TAG, "USB network netif detached (address=%u)", device.info.address);
}
#endif // ESP_USB_HOST_HAS_ESP_NETIF

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

bool EspUsbHost::submitAudioClockSampleRate(DeviceState &device, uint8_t clockSourceId, uint32_t sampleRate)
{
  if (!clientHandle_ || !device.handle || clockSourceId == 0 ||
      device.audioControlInterfaceNumber == 0xff)
  {
    return false;
  }

  const AudioClockSourceState *clock = findAudioClockSource(device, clockSourceId);
  if (clock && (clock->controls & USB_AUDIO_CLOCK_FREQ_CONTROL_MASK) != USB_AUDIO_CLOCK_FREQ_CONTROL_RW)
  {
    // A clock whose sample frequency control is read-only (or absent) runs at a
    // fixed rate. Skipping the request is not a failure: the caller's rate came
    // from the RANGE query, so it already matches what the device runs at.
    ESP_LOGI(TAG, "USB Audio clock=%u sample frequency is not programmable (controls=0x%02x)",
             clockSourceId,
             clock->controls);
    return true;
  }

  static constexpr size_t AUDIO_CLOCK_SAMPLE_RATE_LENGTH = 4;

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + AUDIO_CLOCK_SAMPLE_RATE_LENGTH, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Audio clock SET_CUR) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_AUDIO_ENTITY_SET_REQUEST_TYPE;
  setup->bRequest = USB_AUDIO_REQUEST_CUR;
  setup->wValue = static_cast<uint16_t>(USB_AUDIO_CLOCK_SAM_FREQ_CONTROL) << 8;
  setup->wIndex = static_cast<uint16_t>(static_cast<uint16_t>(clockSourceId) << 8) |
                  device.audioControlInterfaceNumber;
  setup->wLength = AUDIO_CLOCK_SAMPLE_RATE_LENGTH;

  uint8_t *frequency = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
  frequency[0] = sampleRate & 0xff;
  frequency[1] = (sampleRate >> 8) & 0xff;
  frequency[2] = (sampleRate >> 16) & 0xff;
  frequency[3] = (sampleRate >> 24) & 0xff;

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + AUDIO_CLOCK_SAMPLE_RATE_LENGTH;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Audio clock SET_CUR clock=%u) failed: %s",
             clockSourceId,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "Audio clock SET_CUR submitted clock=%u rate=%lu",
           clockSourceId,
           static_cast<unsigned long>(sampleRate));
  return true;
}

bool EspUsbHost::applyAudioStreamSampleRate(DeviceState &device,
                                            const EspUsbHostAudioStreamInfo &stream,
                                            uint32_t sampleRate)
{
  if (stream.protocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2)
  {
    return submitAudioSamplingFrequency(device, stream.endpointAddress, sampleRate);
  }
  if (stream.clockSourceId == 0)
  {
    // No clock entity was found for this stream, so there is nothing to program.
    // The device keeps whatever rate it defaults to.
    ESP_LOGD(TAG, "USB Audio UAC2 stream iface=%u ep=0x%02x has no clock source",
             stream.interfaceNumber,
             stream.endpointAddress);
    return true;
  }
  return submitAudioClockSampleRate(device, stream.clockSourceId, sampleRate);
}

bool EspUsbHost::submitAudioClockSampleRateRange(DeviceState &device, uint8_t clockSourceId, uint8_t attemptIndex)
{
  if (!clientHandle_ || !device.handle || clockSourceId == 0 ||
      device.audioControlInterfaceNumber == 0xff)
  {
    return false;
  }

  // attemptIndex selects the wLength strategy:
  //   0                          - 2-byte probe that reads wNumSubRanges only
  //   1..MAX_AUDIO_SAMPLE_RATES  - full RANGE payload for that many subranges
  //   AUDIO_CLOCK_CUR_ATTEMPT    - GET CUR fallback, learns just the active rate
  // See nextAudioClockRangeAttempt() for the order they are tried in.
  const bool current = attemptIndex == AUDIO_CLOCK_CUR_ATTEMPT;
  const size_t length = current ? 4 : (attemptIndex == 0 ? 2 : 2 + static_cast<size_t>(attemptIndex) * 12);

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Audio clock RANGE) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  AudioClockRangeTransferContext *context = new (std::nothrow) AudioClockRangeTransferContext();
  if (!context)
  {
    usb_host_transfer_free(transfer);
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }
  context->host = this;
  context->address = device.info.address;
  context->clockSourceId = clockSourceId;
  context->attemptIndex = attemptIndex;

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = USB_AUDIO_ENTITY_GET_REQUEST_TYPE;
  setup->bRequest = current ? USB_AUDIO_REQUEST_CUR : USB_AUDIO_REQUEST_RANGE;
  setup->wValue = static_cast<uint16_t>(USB_AUDIO_CLOCK_SAM_FREQ_CONTROL) << 8;
  setup->wIndex = static_cast<uint16_t>(static_cast<uint16_t>(clockSourceId) << 8) |
                  device.audioControlInterfaceNumber;
  setup->wLength = static_cast<uint16_t>(length);

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = audioClockRangeTransferCallback;
  transfer->context = context;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Audio clock %s clock=%u) failed: %s",
             current ? "GET CUR" : "RANGE",
             clockSourceId,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    delete context;
    return false;
  }
  return true;
}

void EspUsbHost::queryAudioClockSampleRates(DeviceState &device)
{
  uint8_t queried[ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES] = {};
  uint8_t queriedCount = 0;

  for (uint8_t i = 0; i < device.audioStreamInfoCount; i++)
  {
    const EspUsbHostAudioStreamInfo &stream = device.audioStreamInfos[i];
    if (stream.protocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 || stream.clockSourceId == 0)
    {
      continue;
    }
    bool alreadyQueried = false;
    for (uint8_t j = 0; j < queriedCount; j++)
    {
      if (queried[j] == stream.clockSourceId)
      {
        alreadyQueried = true;
        break;
      }
    }
    if (alreadyQueried || queriedCount >= ESP_USB_HOST_MAX_AUDIO_CLOCK_SOURCES)
    {
      continue;
    }
    queried[queriedCount++] = stream.clockSourceId;
    submitAudioClockSampleRateRange(device, stream.clockSourceId, AUDIO_CLOCK_FIRST_ATTEMPT);
  }
}

void EspUsbHost::applyAudioClockSampleRates(DeviceState &device,
                                            uint8_t clockSourceId,
                                            const uint32_t *rates,
                                            size_t rateCount,
                                            uint32_t currentRate)
{
  uint32_t resolved[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
  size_t resolvedCount = 0;
  for (size_t i = 0; i < rateCount && resolvedCount < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++)
  {
    if (rates && rates[i] > 0)
    {
      resolved[resolvedCount++] = rates[i];
    }
  }
  if (resolvedCount == 0)
  {
    if (currentRate == 0)
    {
      return;
    }
    resolved[resolvedCount++] = currentRate;
  }

  uint32_t min = resolved[0];
  uint32_t max = resolved[0];
  for (size_t i = 1; i < resolvedCount; i++)
  {
    min = resolved[i] < min ? resolved[i] : min;
    max = resolved[i] > max ? resolved[i] : max;
  }

  uint8_t updated = 0;
  for (uint8_t i = 0; i < device.audioStreamInfoCount; i++)
  {
    EspUsbHostAudioStreamInfo &stream = device.audioStreamInfos[i];
    if (stream.protocol != ESP_USB_HOST_AUDIO_PROTOCOL_UAC2 ||
        stream.clockSourceId != clockSourceId)
    {
      continue;
    }
    stream.sampleRateCount = static_cast<uint8_t>(resolvedCount);
    for (size_t j = 0; j < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; j++)
    {
      stream.sampleRates[j] = j < resolvedCount ? resolved[j] : 0;
    }
    stream.sampleRateMin = min;
    stream.sampleRateMax = max;
    stream.sampleRateResolution = 0;
    // Prefer the rate the device reports as active, so the stream's nominal rate
    // matches the hardware before anything is started.
    stream.sampleRate = resolved[0];
    if (currentRate > 0 && espUsbHostAudioStreamSupportsSampleRate(stream, currentRate))
    {
      stream.sampleRate = currentRate;
    }
    updated++;
  }

  ESP_LOGI(TAG, "USB Audio clock=%u sample rates: count=%u first=%lu min=%lu max=%lu streams=%u",
           clockSourceId,
           static_cast<unsigned>(resolvedCount),
           static_cast<unsigned long>(resolved[0]),
           static_cast<unsigned long>(min),
           static_cast<unsigned long>(max),
           updated);
}

void EspUsbHost::audioClockRangeTransferCallback(usb_transfer_t *transfer)
{
  AudioClockRangeTransferContext *context = static_cast<AudioClockRangeTransferContext *>(transfer->context);
  EspUsbHost *host = context ? context->host : nullptr;
  DeviceState *device = host ? host->findDevice(context->address) : nullptr;

  size_t payload = transfer->actual_num_bytes;
  payload = payload > USB_SETUP_PACKET_SIZE ? payload - USB_SETUP_PACKET_SIZE : 0;
  const uint8_t *data = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
  const bool completed = transfer->status == USB_TRANSFER_STATUS_COMPLETED;

  bool handled = false;
  if (device && completed)
  {
    if (context->attemptIndex == AUDIO_CLOCK_CUR_ATTEMPT)
    {
      if (payload >= 4)
      {
        host->applyAudioClockSampleRates(*device, context->clockSourceId, nullptr, 0,
                                         espUsbHostAudioReadU32(data));
        handled = true;
      }
    }
    else
    {
      uint32_t rates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
      const size_t count = espUsbHostAudioDecodeSampleRateRange(data, payload, rates,
                                                                ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES);
      // wNumSubRanges is authoritative even when the payload was cut short by the
      // requested wLength. Asking again for the size the device named is what
      // turns the 2-byte probe, and a too-small first guess, into a full answer.
      const size_t declared = espUsbHostAudioRangeDeclaredCount(data, payload);
      const size_t wanted = declared > ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES
                                ? ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES
                                : declared;
      if (wanted > 0 && wanted != context->attemptIndex && count < wanted)
      {
        handled = host->submitAudioClockSampleRateRange(*device, context->clockSourceId,
                                                        static_cast<uint8_t>(wanted));
      }
      if (!handled && count > 0)
      {
        host->applyAudioClockSampleRates(*device, context->clockSourceId, rates, count, 0);
        handled = true;
      }
    }
  }

  if (!handled && device)
  {
    ESP_LOGD(TAG, "Audio clock RANGE attempt=%u clock=%u status=%d bytes=%u",
             context->attemptIndex,
             context->clockSourceId,
             transfer->status,
             static_cast<unsigned>(payload));
    const uint8_t next = nextAudioClockRangeAttempt(context->attemptIndex);
    if (context->attemptIndex == AUDIO_CLOCK_CUR_ATTEMPT)
    {
      ESP_LOGW(TAG, "USB Audio clock=%u sample rates could not be read", context->clockSourceId);
    }
    else
    {
      host->submitAudioClockSampleRateRange(*device, context->clockSourceId, next);
    }
  }

  usb_host_transfer_free(transfer);
  delete context;
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

EspUsbHostCdcSerial::~EspUsbHostCdcSerial()
{
  if (attached_)
  {
    host_.detachCdcSerial(this);
    attached_ = false;
  }
  free(rxBuffer_);
  rxBuffer_ = nullptr;
}

bool EspUsbHostCdcSerial::allocateRxBuffer()
{
  if (rxBuffer_)
  {
    return true;
  }
  rxBuffer_ = static_cast<uint8_t *>(malloc(rxBufferSize_));
  if (!rxBuffer_)
  {
    ESP_LOGE(TAG, "cdc serial rx buffer alloc failed (%u bytes)", static_cast<unsigned>(rxBufferSize_));
    return false;
  }
  rxHead_ = 0;
  rxTail_ = 0;
  return true;
}

bool EspUsbHostCdcSerial::setRxBufferSize(size_t size)
{
  // The USB client task writes into the ring, so the buffer can only be swapped
  // while nothing is attached. Call this before begin(), or after end().
  if (attached_ || size < 2)
  {
    return false;
  }
  if (rxBuffer_ && size == rxBufferSize_)
  {
    return true;
  }
  uint8_t *buffer = static_cast<uint8_t *>(malloc(size));
  if (!buffer)
  {
    return false;
  }
  free(rxBuffer_);
  rxBuffer_ = buffer;
  rxBufferSize_ = size;
  rxHead_ = 0;
  rxTail_ = 0;
  return true;
}

size_t EspUsbHostCdcSerial::rxBufferSize() const
{
  return rxBufferSize_;
}

bool EspUsbHostCdcSerial::begin(uint32_t baud)
{
  if (!allocateRxBuffer())
  {
    return false;
  }

  portENTER_CRITICAL(&rxMux_);
  rxHead_ = 0;
  rxTail_ = 0;
  portEXIT_CRITICAL(&rxMux_);

  host_.attachCdcSerial(this);
  attached_ = true;
  return host_.setSerialBaudRate(baud, address_);
}

void EspUsbHostCdcSerial::end()
{
  host_.detachCdcSerial(this);
  attached_ = false;
}

bool EspUsbHostCdcSerial::connected() const
{
  return host_.serialReady(address_);
}

int EspUsbHostCdcSerial::available()
{
  portENTER_CRITICAL(&rxMux_);
  const size_t count = rxHead_ >= rxTail_ ? rxHead_ - rxTail_ : rxBufferSize_ - rxTail_ + rxHead_;
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
  // Without the asynchronous queue there is nothing to wait for: each write is
  // already submitted to the driver and the completion is not tracked.
  if (host_.serialWriteQueueReady(address_))
  {
    host_.serialWriteFlush(ESP_USB_HOST_SERIAL_WRITE_DEFAULT_TIMEOUT_MS, address_);
  }
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
  if (!rxBuffer_)
  {
    return;
  }
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
  if (index >= rxBufferSize_)
  {
    return 0;
  }
  return index;
}
