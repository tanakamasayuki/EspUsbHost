// CCID (USB smart card reader, bInterfaceClass 0x0b) support for EspUsbHost.
// Kept in its own translation unit so the already large EspUsbHost.cpp does not
// grow further; the members defined here are declared in EspUsbHost.h.

#include "EspUsbHost.h"

#include <string.h>

#if defined(CONFIG_CACHE_L1_CACHE_LINE_SIZE) && CONFIG_CACHE_L1_CACHE_LINE_SIZE > 0
#include "esp_cache.h"
#define ESP_USB_HOST_CCID_DMA_CACHE_SYNC 1
#endif

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
static const char *TAG = "EspUsbHost";
#endif

namespace
{

constexpr uint8_t USB_CLASS_CCID_VALUE = 0x0b;
constexpr uint8_t USB_CCID_SUBCLASS_BULK = 0x00;
constexpr uint8_t USB_CCID_PROTOCOL_BULK = 0x00;
constexpr uint8_t USB_CCID_DESCRIPTOR_LENGTH = 54;

// PC_to_RDR message types used by this API. Any other type can be sent with
// ccidMessage().
constexpr uint8_t CCID_PC_TO_RDR_ICC_POWER_ON = 0x62;
constexpr uint8_t CCID_PC_TO_RDR_ICC_POWER_OFF = 0x63;
constexpr uint8_t CCID_PC_TO_RDR_GET_SLOT_STATUS = 0x65;
constexpr uint8_t CCID_PC_TO_RDR_ESCAPE = 0x6e;
constexpr uint8_t CCID_PC_TO_RDR_XFR_BLOCK = 0x6f;
constexpr uint8_t CCID_PC_TO_RDR_ABORT = 0x72;

// RDR_to_PC message types.
constexpr uint8_t CCID_RDR_TO_PC_DATA_BLOCK = 0x80;
constexpr uint8_t CCID_RDR_TO_PC_SLOT_STATUS = 0x81;
constexpr uint8_t CCID_RDR_TO_PC_ESCAPE = 0x83;

// Interrupt IN message types.
constexpr uint8_t CCID_RDR_TO_PC_NOTIFY_SLOT_CHANGE = 0x50;
constexpr uint8_t CCID_RDR_TO_PC_HARDWARE_ERROR = 0x51;

// CCID class requests (bmRequestType 0x21 / 0xa1, interface recipient).
constexpr uint8_t CCID_CLASS_REQUEST_ABORT = 0x01;
constexpr uint8_t CCID_ABORT_REQUEST_TYPE = 0x21;

constexpr size_t CCID_HEADER_SIZE = 10;
// A reader may ask for more time repeatedly; stop extending eventually so a
// wedged reader cannot block the caller forever.
constexpr uint8_t CCID_MAX_TIME_EXTENSIONS = 8;
// Responses carrying another command's bSeq are dropped. Bound the drops so a
// desynchronized reader still returns an error instead of spinning.
constexpr uint8_t CCID_MAX_SEQUENCE_MISMATCHES = 4;
// Zero-length bulk IN completions carry no message; bound them too.
constexpr uint8_t CCID_MAX_EMPTY_READS = 8;
constexpr size_t CCID_MAX_BUFFER_SIZE = 4096;

struct CcidSyncContext
{
  SemaphoreHandle_t done = nullptr;
  usb_transfer_status_t status = USB_TRANSFER_STATUS_ERROR;
  size_t actualLength = 0;
};

void ccidSyncTransferCallback(usb_transfer_t *transfer)
{
  CcidSyncContext *context = static_cast<CcidSyncContext *>(transfer->context);
  if (!context)
  {
    return;
  }
  context->status = transfer->status;
  context->actualLength = transfer->actual_num_bytes;
  xSemaphoreGive(context->done);
}

void ccidCacheSyncBeforeInTransfer(usb_transfer_t *transfer)
{
#if defined(ESP_USB_HOST_CCID_DMA_CACHE_SYNC)
  if (!transfer || !transfer->data_buffer || transfer->data_buffer_size == 0)
  {
    return;
  }
  const esp_err_t err = esp_cache_msync(transfer->data_buffer,
                                        transfer->data_buffer_size,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "esp_cache_msync(CCID IN buffer) failed: %s", esp_err_to_name(err));
  }
#else
  (void)transfer;
#endif
}

uint32_t readLe32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe32(uint8_t *data, uint32_t value)
{
  data[0] = static_cast<uint8_t>(value & 0xff);
  data[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  data[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  data[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

// Milliseconds left before deadline, 0 when it has passed. millis() wrap is
// handled by the unsigned subtraction.
uint32_t remainingMs(uint32_t deadline)
{
  const uint32_t now = millis();
  return static_cast<int32_t>(deadline - now) > 0 ? deadline - now : 0;
}

} // namespace

void EspUsbHost::parseCcidClassDescriptor(DeviceState &device, const uint8_t *data)
{
  if (!data || data[0] < USB_CCID_DESCRIPTOR_LENGTH)
  {
    ESP_LOGW(TAG, "CCID class descriptor too short: length=%u", data ? data[0] : 0);
    return;
  }

  device.ccidHasClassDescriptor = true;
  device.ccidDescriptorInterfaceNumber = currentInterfaceNumber_;
  device.ccidBcd = static_cast<uint16_t>(data[2]) | (static_cast<uint16_t>(data[3]) << 8);
  device.ccidSlotCount = static_cast<uint8_t>(data[4] + 1);
  device.ccidVoltageSupport = data[5];
  device.ccidProtocols = readLe32(&data[6]);
  device.ccidFeatures = readLe32(&data[40]);
  device.ccidMaxMessageLength = readLe32(&data[44]);
  device.ccidMaxBusySlots = data[53];

  ESP_LOGI(TAG, "CCID class descriptor: iface=%u slots=%u protocols=0x%08lx features=0x%08lx maxMessage=%lu voltage=0x%02x",
           device.ccidDescriptorInterfaceNumber,
           device.ccidSlotCount,
           static_cast<unsigned long>(device.ccidProtocols),
           static_cast<unsigned long>(device.ccidFeatures),
           static_cast<unsigned long>(device.ccidMaxMessageLength),
           device.ccidVoltageSupport);
}

EspUsbHost::DeviceState *EspUsbHost::findCcidDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findCcidDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findCcidDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasCcidInterface)
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

EspUsbHost::DeviceState *EspUsbHost::findCcidCandidate(uint8_t address, uint8_t interfaceNumber)
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
      if (intf.interfaceClass != USB_CLASS_CCID_VALUE)
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

bool EspUsbHost::ccidOpen(uint8_t address, uint8_t interfaceNumber)
{
  DeviceState *device = findCcidCandidate(address, interfaceNumber);
  if (!device)
  {
    ESP_LOGW(TAG, "ccidOpen() no CCID interface");
    return false;
  }

  if (device->hasCcidInterface)
  {
    if (interfaceNumber != 0xff && device->ccidInterfaceNumber != interfaceNumber)
    {
      ESP_LOGW(TAG, "ccidOpen() another CCID interface is already open");
      return false;
    }
    return true;
  }

  uint8_t selectedInterface = 0xff;
  EspUsbHostEndpointInfo inEndpoint;
  EspUsbHostEndpointInfo outEndpoint;
  EspUsbHostEndpointInfo interruptEndpoint;
  bool hasInterrupt = false;

  for (uint8_t i = 0; i < device->interfaceInfoCount && selectedInterface == 0xff; i++)
  {
    const EspUsbHostInterfaceInfo &intf = device->interfaceInfos[i];
    if (intf.interfaceClass != USB_CLASS_CCID_VALUE)
    {
      continue;
    }
    if (interfaceNumber != 0xff && intf.number != interfaceNumber)
    {
      continue;
    }
    // ICCD variants (protocol 0x01 / 0x02) move the messages onto the control
    // pipe and are not handled by this API.
    if (intf.interfaceSubClass != USB_CCID_SUBCLASS_BULK ||
        intf.interfaceProtocol != USB_CCID_PROTOCOL_BULK)
    {
      ESP_LOGW(TAG, "ccidOpen() skipping non-bulk CCID interface %u subclass=0x%02x protocol=0x%02x",
               intf.number,
               intf.interfaceSubClass,
               intf.interfaceProtocol);
      continue;
    }

    bool foundIn = false;
    bool foundOut = false;
    bool foundInterrupt = false;
    EspUsbHostEndpointInfo candidateIn;
    EspUsbHostEndpointInfo candidateOut;
    EspUsbHostEndpointInfo candidateInterrupt;
    for (uint8_t e = 0; e < device->endpointInfoCount; e++)
    {
      const EspUsbHostEndpointInfo &ep = device->endpointInfos[e];
      if (ep.interfaceNumber != intf.number)
      {
        continue;
      }
      const bool isIn = (ep.address & 0x80) != 0;
      const uint8_t type = ep.attributes & 0x03;
      if (type == 0x02 && isIn && !foundIn)
      {
        candidateIn = ep;
        foundIn = true;
      }
      else if (type == 0x02 && !isIn && !foundOut)
      {
        candidateOut = ep;
        foundOut = true;
      }
      else if (type == 0x03 && isIn && !foundInterrupt)
      {
        candidateInterrupt = ep;
        foundInterrupt = true;
      }
    }

    if (!foundIn || !foundOut)
    {
      ESP_LOGW(TAG, "ccidOpen() interface %u has no bulk IN/OUT pair", intf.number);
      continue;
    }

    selectedInterface = intf.number;
    inEndpoint = candidateIn;
    outEndpoint = candidateOut;
    interruptEndpoint = candidateInterrupt;
    hasInterrupt = foundInterrupt;
  }

  if (selectedInterface == 0xff)
  {
    ESP_LOGW(TAG, "ccidOpen() no usable CCID interface");
    return false;
  }

  size_t bufferSize = ESP_USB_HOST_CCID_BUFFER_SIZE;
  if (device->ccidHasClassDescriptor &&
      device->ccidMaxMessageLength > bufferSize &&
      device->ccidMaxMessageLength <= CCID_MAX_BUFFER_SIZE)
  {
    bufferSize = device->ccidMaxMessageLength;
  }
  // Round up to a whole number of bulk IN packets so a maximum length response
  // never has to be truncated mid-packet.
  if (inEndpoint.maxPacketSize > 0)
  {
    bufferSize = ((bufferSize + inEndpoint.maxPacketSize - 1) / inEndpoint.maxPacketSize) *
                 inEndpoint.maxPacketSize;
  }

  uint8_t *buffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (!buffer)
  {
    ESP_LOGW(TAG, "ccidOpen() cannot allocate %u byte message buffer", static_cast<unsigned>(bufferSize));
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  if (!device->ccidLock)
  {
    device->ccidLock = xSemaphoreCreateMutex();
    if (!device->ccidLock)
    {
      free(buffer);
      setLastError(ESP_ERR_NO_MEM);
      return false;
    }
  }

  bool interfaceClaimed = false;
  for (uint8_t i = 0; i < device->interfaceCount; i++)
  {
    if (device->interfaces[i] == selectedInterface)
    {
      interfaceClaimed = true;
      break;
    }
  }

  if (!interfaceClaimed)
  {
    const esp_err_t err = usb_host_interface_claim(clientHandle_, device->handle, selectedInterface, 0);
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
      ESP_LOGW(TAG, "usb_host_interface_claim(CCID iface=%u) failed: %s",
               selectedInterface,
               esp_err_to_name(err));
      free(buffer);
      setLastError(err);
      return false;
    }
    if (device->interfaceCount < sizeof(device->interfaces))
    {
      device->interfaces[device->interfaceCount++] = selectedInterface;
    }
    // Bulk IN, bulk OUT, and the interrupt IN when it is actually used.
    device->endpointChannelCount = static_cast<uint8_t>(device->endpointChannelCount + (hasInterrupt ? 3 : 2));
  }

  free(device->ccidBuffer);
  device->ccidBuffer = buffer;
  device->ccidBufferSize = bufferSize;
  device->ccidResponseLength = 0;
  device->ccidInterfaceNumber = selectedInterface;
  device->ccidInEndpointAddress = inEndpoint.address;
  device->ccidInPacketSize = inEndpoint.maxPacketSize;
  device->ccidOutEndpointAddress = outEndpoint.address;
  device->ccidOutPacketSize = outEndpoint.maxPacketSize;
  device->ccidInterruptEndpointAddress = hasInterrupt ? interruptEndpoint.address : 0;
  device->ccidInterruptPacketSize = hasInterrupt ? interruptEndpoint.maxPacketSize : 0;
  device->ccidSequence = 0;
  device->ccidError = 0;
  device->ccidAtrLength = 0;
  device->ccidSlotPresentMask = 0;
  device->ccidSlotKnownMask = 0;
  if (!device->ccidHasClassDescriptor)
  {
    // Without a class descriptor assume the common single-slot, short-APDU
    // reader; the message buffer already bounds what can be sent.
    device->ccidSlotCount = 1;
    device->ccidMaxMessageLength = bufferSize;
  }
  device->hasCcidInterface = true;

  if (hasInterrupt)
  {
    EndpointState *endpoint = findEndpoint(device->handle, interruptEndpoint.address);
    if (!endpoint)
    {
      endpoint = allocateEndpoint(*device);
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available for CCID interrupt IN");
        // The bulk pipes still work; slot-change callbacks simply stay silent.
        device->ccidInterruptEndpointAddress = 0;
      }
      else
      {
        const esp_err_t err = usb_host_transfer_alloc(interruptEndpoint.maxPacketSize, 0, &endpoint->transfer);
        if (err != ESP_OK)
        {
          endpoint->inUse = false;
          ESP_LOGW(TAG, "usb_host_transfer_alloc(CCID interrupt IN) failed: %s", esp_err_to_name(err));
          device->ccidInterruptEndpointAddress = 0;
        }
        else
        {
          endpoint->address = interruptEndpoint.address;
          endpoint->interfaceNumber = selectedInterface;
          endpoint->alternate = 0;
          endpoint->interfaceClass = USB_CLASS_CCID_VALUE;
          endpoint->interfaceSubClass = USB_CCID_SUBCLASS_BULK;
          endpoint->interfaceProtocol = USB_CCID_PROTOCOL_BULK;
          endpoint->transfer->device_handle = device->handle;
          endpoint->transfer->bEndpointAddress = interruptEndpoint.address;
          endpoint->transfer->callback = transferCallback;
          endpoint->transfer->context = this;
          endpoint->transfer->num_bytes = interruptEndpoint.maxPacketSize;
          if (!submitInputTransfer(*endpoint))
          {
            ESP_LOGW(TAG, "CCID interrupt IN transfer could not be submitted");
          }
        }
      }
    }
  }

  ESP_LOGI(TAG, "CCID interface ready: address=%u iface=%u in=0x%02x out=0x%02x interrupt=0x%02x slots=%u buffer=%u",
           device->info.address,
           selectedInterface,
           device->ccidInEndpointAddress,
           device->ccidOutEndpointAddress,
           device->ccidInterruptEndpointAddress,
           device->ccidSlotCount,
           static_cast<unsigned>(bufferSize));
  return true;
}

void EspUsbHost::releaseCcidInterface(DeviceState &device)
{
  device.hasCcidInterface = false;
  device.ccidResponseLength = 0;
  device.ccidAtrLength = 0;
  device.ccidSlotPresentMask = 0;
  device.ccidSlotKnownMask = 0;
  free(device.ccidBuffer);
  device.ccidBuffer = nullptr;
  device.ccidBufferSize = 0;
}

void EspUsbHost::ccidClose(uint8_t address)
{
  DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return;
  }

  // Take the lock so a command in flight finishes before its buffer goes away.
  if (device->ccidLock)
  {
    xSemaphoreTake(device->ccidLock, portMAX_DELAY);
  }
  releaseCcidInterface(*device);
  if (device->ccidLock)
  {
    xSemaphoreGive(device->ccidLock);
  }
  ESP_LOGI(TAG, "CCID interface closed: address=%u", device->info.address);
}

bool EspUsbHost::ccidReady(uint8_t address) const
{
  return findCcidDevice(address) != nullptr;
}

bool EspUsbHost::ccidGetInterface(EspUsbHostCcidInterface &info, uint8_t address) const
{
  const DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return false;
  }

  info = EspUsbHostCcidInterface();
  info.address = device->info.address;
  info.interfaceNumber = device->ccidInterfaceNumber;
  info.inEndpoint = device->ccidInEndpointAddress;
  info.outEndpoint = device->ccidOutEndpointAddress;
  info.interruptEndpoint = device->ccidInterruptEndpointAddress;
  info.inMaxPacketSize = device->ccidInPacketSize;
  info.outMaxPacketSize = device->ccidOutPacketSize;
  info.hasClassDescriptor = device->ccidHasClassDescriptor;
  info.bcdCCID = device->ccidBcd;
  info.slotCount = device->ccidSlotCount;
  info.voltageSupport = device->ccidVoltageSupport;
  info.protocols = device->ccidProtocols;
  info.features = device->ccidFeatures;
  info.maxMessageLength = device->ccidMaxMessageLength;
  info.maxBusySlots = device->ccidMaxBusySlots;
  switch ((device->ccidFeatures >> 16) & 0x07)
  {
  case 0x01:
    info.exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_TPDU;
    break;
  case 0x02:
    info.exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_SHORT_APDU;
    break;
  case 0x04:
    info.exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_EXTENDED_APDU;
    break;
  default:
    info.exchangeLevel = ESP_USB_HOST_CCID_EXCHANGE_CHARACTER;
    break;
  }
  return true;
}

uint8_t EspUsbHost::ccidSlotCount(uint8_t address) const
{
  const DeviceState *device = findCcidDevice(address);
  return device ? device->ccidSlotCount : 0;
}

uint8_t EspUsbHost::ccidLastError(uint8_t address) const
{
  const DeviceState *device = findCcidDevice(address);
  return device ? device->ccidError : 0;
}

bool EspUsbHost::ccidBulkOut(DeviceState &device, const uint8_t *data, size_t length, uint32_t timeoutMs)
{
  CcidSyncContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(length > 0 ? length : 1, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(CCID bulk OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  memcpy(transfer->data_buffer, data, length);
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = device.ccidOutEndpointAddress;
  transfer->callback = ccidSyncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(CCID bulk OUT ep=0x%02x) failed: %s",
             device.ccidOutEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  if (!done)
  {
    ESP_LOGW(TAG, "CCID bulk OUT timeout ep=0x%02x", device.ccidOutEndpointAddress);
    usb_host_endpoint_halt(device.handle, device.ccidOutEndpointAddress);
    usb_host_endpoint_flush(device.handle, device.ccidOutEndpointAddress);
    // The HCD owns the transfer until the flushed URB callback runs.
    xSemaphoreTake(context.done, portMAX_DELAY);
    usb_host_endpoint_clear(device.handle, device.ccidOutEndpointAddress);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  const bool ok = context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (!ok)
  {
    ESP_LOGW(TAG, "CCID bulk OUT failed ep=0x%02x status=%d",
             device.ccidOutEndpointAddress,
             context.status);
    usb_host_endpoint_clear(device.handle, device.ccidOutEndpointAddress);
    setLastError(ESP_FAIL);
  }
  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  return ok;
}

// Reads one bulk IN transfer and appends it to device.ccidBuffer. A CCID
// response can span several 64-byte packets, so the caller loops until the
// dwLength in the header is satisfied.
bool EspUsbHost::ccidBulkIn(DeviceState &device, uint32_t timeoutMs)
{
  if (device.ccidResponseLength >= device.ccidBufferSize)
  {
    return false;
  }

  size_t request = device.ccidBufferSize - device.ccidResponseLength;
  if (device.ccidInPacketSize > 0)
  {
    request = (request / device.ccidInPacketSize) * device.ccidInPacketSize;
    if (request == 0)
    {
      request = device.ccidInPacketSize;
    }
  }

  CcidSyncContext context;
  context.done = xSemaphoreCreateBinary();
  if (!context.done)
  {
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(request, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(CCID bulk IN) failed: %s", esp_err_to_name(err));
    setLastError(err);
    vSemaphoreDelete(context.done);
    return false;
  }

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = device.ccidInEndpointAddress;
  transfer->callback = ccidSyncTransferCallback;
  transfer->context = &context;
  transfer->num_bytes = request;
  ccidCacheSyncBeforeInTransfer(transfer);

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(CCID bulk IN ep=0x%02x) failed: %s",
             device.ccidInEndpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    return false;
  }

  const bool done = xSemaphoreTake(context.done, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  if (!done)
  {
    ESP_LOGW(TAG, "CCID bulk IN timeout ep=0x%02x", device.ccidInEndpointAddress);
    usb_host_endpoint_halt(device.handle, device.ccidInEndpointAddress);
    usb_host_endpoint_flush(device.handle, device.ccidInEndpointAddress);
    xSemaphoreTake(context.done, portMAX_DELAY);
    usb_host_endpoint_clear(device.handle, device.ccidInEndpointAddress);
    usb_host_transfer_free(transfer);
    vSemaphoreDelete(context.done);
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  bool ok = context.status == USB_TRANSFER_STATUS_COMPLETED;
  if (ok)
  {
    size_t copy = context.actualLength;
    if (copy > device.ccidBufferSize - device.ccidResponseLength)
    {
      copy = device.ccidBufferSize - device.ccidResponseLength;
    }
    memcpy(device.ccidBuffer + device.ccidResponseLength, transfer->data_buffer, copy);
    device.ccidResponseLength += copy;
  }
  else
  {
    ESP_LOGW(TAG, "CCID bulk IN failed ep=0x%02x status=%d",
             device.ccidInEndpointAddress,
             context.status);
    usb_host_endpoint_clear(device.handle, device.ccidInEndpointAddress);
    setLastError(ESP_FAIL);
  }

  usb_host_transfer_free(transfer);
  vSemaphoreDelete(context.done);
  return ok;
}

bool EspUsbHost::ccidExchange(DeviceState &device,
                              uint8_t messageType,
                              uint8_t slot,
                              const uint8_t messageSpecific[3],
                              const uint8_t *data,
                              size_t length,
                              EspUsbHostCcidResponse &response,
                              uint32_t timeoutMs)
{
  if (!device.ccidBuffer || CCID_HEADER_SIZE + length > device.ccidBufferSize)
  {
    ESP_LOGW(TAG, "CCID message of %u bytes exceeds the %u byte buffer",
             static_cast<unsigned>(length),
             static_cast<unsigned>(device.ccidBufferSize));
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }

  const uint8_t sequence = device.ccidSequence++;
  uint8_t *message = device.ccidBuffer;
  message[0] = messageType;
  writeLe32(&message[1], static_cast<uint32_t>(length));
  message[5] = slot;
  message[6] = sequence;
  message[7] = messageSpecific ? messageSpecific[0] : 0;
  message[8] = messageSpecific ? messageSpecific[1] : 0;
  message[9] = messageSpecific ? messageSpecific[2] : 0;
  if (length > 0)
  {
    memcpy(&message[CCID_HEADER_SIZE], data, length);
  }

  const uint32_t deadline = millis() + timeoutMs;
  if (!ccidBulkOut(device, message, CCID_HEADER_SIZE + length, timeoutMs))
  {
    return false;
  }

  uint8_t timeExtensions = 0;
  uint8_t sequenceMismatches = 0;
  uint8_t emptyReads = 0;
  device.ccidResponseLength = 0;

  while (true)
  {
    const uint32_t wait = remainingMs(deadline);
    if (wait == 0)
    {
      ESP_LOGW(TAG, "CCID response timeout type=0x%02x seq=%u", messageType, sequence);
      setLastError(ESP_ERR_TIMEOUT);
      return false;
    }
    const size_t before = device.ccidResponseLength;
    if (!ccidBulkIn(device, wait))
    {
      return false;
    }
    // A reader answering with zero-length packets would otherwise keep this
    // loop resubmitting until the deadline.
    if (device.ccidResponseLength == before)
    {
      if (++emptyReads > CCID_MAX_EMPTY_READS)
      {
        ESP_LOGW(TAG, "CCID reader returned only empty packets type=0x%02x", messageType);
        setLastError(ESP_FAIL);
        return false;
      }
      continue;
    }
    emptyReads = 0;
    if (device.ccidResponseLength < CCID_HEADER_SIZE)
    {
      continue;
    }

    const uint32_t payloadLength = readLe32(&device.ccidBuffer[1]);
    if (payloadLength > device.ccidBufferSize - CCID_HEADER_SIZE)
    {
      ESP_LOGW(TAG, "CCID response of %lu bytes exceeds the %u byte buffer",
               static_cast<unsigned long>(payloadLength),
               static_cast<unsigned>(device.ccidBufferSize));
      device.ccidResponseLength = 0;
      setLastError(ESP_ERR_INVALID_SIZE);
      return false;
    }
    if (device.ccidResponseLength < CCID_HEADER_SIZE + payloadLength)
    {
      continue; // message spans more packets
    }

    const uint8_t status = device.ccidBuffer[7];
    const uint8_t commandStatus = static_cast<uint8_t>((status >> 6) & 0x03);
    if (device.ccidBuffer[6] != sequence)
    {
      ESP_LOGW(TAG, "CCID response for another command: seq=%u expected=%u",
               device.ccidBuffer[6],
               sequence);
      device.ccidResponseLength = 0;
      if (++sequenceMismatches > CCID_MAX_SEQUENCE_MISMATCHES)
      {
        setLastError(ESP_FAIL);
        return false;
      }
      continue;
    }
    if (commandStatus == ESP_USB_HOST_CCID_COMMAND_TIME_EXTENSION)
    {
      // Not the final response: the reader asked for more time, so wait for
      // another message and give it the same budget again.
      device.ccidResponseLength = 0;
      if (++timeExtensions > CCID_MAX_TIME_EXTENSIONS)
      {
        ESP_LOGW(TAG, "CCID reader kept requesting time extensions type=0x%02x", messageType);
        setLastError(ESP_ERR_TIMEOUT);
        return false;
      }
      ESP_LOGD(TAG, "CCID time extension %u type=0x%02x", timeExtensions, messageType);
      continue;
    }

    response = EspUsbHostCcidResponse();
    response.messageType = device.ccidBuffer[0];
    response.slot = device.ccidBuffer[5];
    response.sequence = device.ccidBuffer[6];
    response.status = status;
    response.error = device.ccidBuffer[8];
    response.chainParameter = device.ccidBuffer[9];
    response.iccStatus = static_cast<EspUsbHostCcidIccStatus>(status & 0x03);
    response.commandStatus = static_cast<EspUsbHostCcidCommandStatus>(commandStatus);
    response.data = payloadLength > 0 ? &device.ccidBuffer[CCID_HEADER_SIZE] : nullptr;
    response.length = payloadLength;

    device.ccidError = commandStatus == ESP_USB_HOST_CCID_COMMAND_FAILED ? response.error : 0;
    if (response.slot < 8)
    {
      device.ccidSlotKnownMask = static_cast<uint8_t>(device.ccidSlotKnownMask | (1u << response.slot));
      if (response.iccStatus == ESP_USB_HOST_CCID_ICC_ABSENT)
      {
        device.ccidSlotPresentMask = static_cast<uint8_t>(device.ccidSlotPresentMask & ~(1u << response.slot));
      }
      else
      {
        device.ccidSlotPresentMask = static_cast<uint8_t>(device.ccidSlotPresentMask | (1u << response.slot));
      }
    }
    if (response.iccStatus == ESP_USB_HOST_CCID_ICC_ABSENT && response.slot == device.ccidAtrSlot)
    {
      device.ccidAtrLength = 0;
    }
    return true;
  }
}

bool EspUsbHost::ccidMessage(uint8_t messageType,
                             const uint8_t *messageSpecific,
                             const uint8_t *data,
                             size_t length,
                             EspUsbHostCcidResponse &response,
                             uint8_t slot,
                             uint8_t address,
                             uint32_t timeoutMs)
{
  DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "CCID command called before ccidOpen()");
    return false;
  }
  if (length > 0 && !data)
  {
    return false;
  }
  if (slot >= device->ccidSlotCount)
  {
    ESP_LOGW(TAG, "CCID slot %u out of range (slots=%u)", slot, device->ccidSlotCount);
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "CCID commands cannot run from USB client task");
    return false;
  }

  if (device->ccidLock && xSemaphoreTake(device->ccidLock, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
  {
    ESP_LOGW(TAG, "CCID command timed out waiting for the reader lock");
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }
  const bool ok = device->hasCcidInterface &&
                  ccidExchange(*device, messageType, slot, messageSpecific, data, length, response, timeoutMs);
  if (device->ccidLock)
  {
    xSemaphoreGive(device->ccidLock);
  }
  return ok;
}

bool EspUsbHost::ccidGetStatus(EspUsbHostCcidStatus &status, uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
  const DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return false;
  }

  EspUsbHostCcidResponse response;
  if (!ccidMessage(CCID_PC_TO_RDR_GET_SLOT_STATUS, nullptr, nullptr, 0, response, slot, address, timeoutMs))
  {
    return false;
  }

  status = EspUsbHostCcidStatus();
  status.address = device->info.address;
  status.slot = response.slot;
  status.iccStatus = response.iccStatus;
  status.commandStatus = response.commandStatus;
  status.error = response.error;
  status.present = response.iccStatus != ESP_USB_HOST_CCID_ICC_ABSENT;
  status.active = response.iccStatus == ESP_USB_HOST_CCID_ICC_ACTIVE;
  // GetSlotStatus reports "failed" for an empty slot on some readers; the ICC
  // status bits are still valid, so this is not treated as a call failure.
  return true;
}

bool EspUsbHost::ccidCardPresent(uint8_t slot, uint8_t address)
{
  EspUsbHostCcidStatus status;
  if (!ccidGetStatus(status, slot, address))
  {
    return false;
  }
  return status.present;
}

bool EspUsbHost::ccidPowerOn(uint8_t *atr,
                             size_t atrCapacity,
                             size_t *atrLength,
                             EspUsbHostCcidVoltage voltage,
                             uint8_t slot,
                             uint8_t address,
                             uint32_t timeoutMs)
{
  DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return false;
  }
  if (atrLength)
  {
    *atrLength = 0;
  }

  const uint8_t messageSpecific[3] = {static_cast<uint8_t>(voltage), 0, 0};
  EspUsbHostCcidResponse response;
  if (!ccidMessage(CCID_PC_TO_RDR_ICC_POWER_ON, messageSpecific, nullptr, 0, response, slot, address, timeoutMs))
  {
    return false;
  }
  if (response.commandStatus != ESP_USB_HOST_CCID_COMMAND_OK ||
      response.messageType != CCID_RDR_TO_PC_DATA_BLOCK)
  {
    ESP_LOGW(TAG, "CCID IccPowerOn failed slot=%u status=0x%02x error=0x%02x",
             slot,
             response.status,
             response.error);
    return false;
  }

  device->ccidAtrLength = static_cast<uint8_t>(response.length < ESP_USB_HOST_CCID_MAX_ATR
                                                   ? response.length
                                                   : ESP_USB_HOST_CCID_MAX_ATR);
  device->ccidAtrSlot = slot;
  if (device->ccidAtrLength > 0)
  {
    memcpy(device->ccidAtr, response.data, device->ccidAtrLength);
  }

  if (atr && atrCapacity > 0)
  {
    const size_t copy = response.length < atrCapacity ? response.length : atrCapacity;
    if (copy > 0)
    {
      memcpy(atr, response.data, copy);
    }
    if (atrLength)
    {
      *atrLength = copy;
    }
  }
  else if (atrLength)
  {
    *atrLength = response.length;
  }
  return true;
}

bool EspUsbHost::ccidPowerOff(uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return false;
  }

  EspUsbHostCcidResponse response;
  if (!ccidMessage(CCID_PC_TO_RDR_ICC_POWER_OFF, nullptr, nullptr, 0, response, slot, address, timeoutMs))
  {
    return false;
  }
  if (slot == device->ccidAtrSlot)
  {
    device->ccidAtrLength = 0;
  }
  return response.commandStatus == ESP_USB_HOST_CCID_COMMAND_OK;
}

size_t EspUsbHost::ccidGetAtr(uint8_t *buffer, size_t capacity, uint8_t slot, uint8_t address) const
{
  const DeviceState *device = findCcidDevice(address);
  if (!device || !buffer || capacity == 0 || device->ccidAtrLength == 0 || device->ccidAtrSlot != slot)
  {
    return 0;
  }
  const size_t copy = device->ccidAtrLength < capacity ? device->ccidAtrLength : capacity;
  memcpy(buffer, device->ccidAtr, copy);
  return copy;
}

bool EspUsbHost::ccidGetCardInfo(EspUsbHostCcidCardInfo &info, uint8_t slot, uint8_t address) const
{
  info = EspUsbHostCcidCardInfo();
  const DeviceState *device = findCcidDevice(address);
  if (!device || device->ccidAtrLength == 0 || device->ccidAtrSlot != slot)
  {
    return false;
  }
  return espUsbHostParseCcidAtr(device->ccidAtr, device->ccidAtrLength, info);
}

bool EspUsbHost::ccidIdentifyCard(EspUsbHostCcidCardInfo &info,
                                  uint8_t slot,
                                  uint8_t address,
                                  uint32_t timeoutMs)
{
  if (!ccidGetCardInfo(info, slot, address))
  {
    return false;
  }
  if (info.standard != ESP_USB_HOST_CCID_CARD_UNKNOWN)
  {
    return true;
  }

  // PC/SC Get UID. Contactless readers answer with the card identifier: an
  // NFCID1 for ISO 14443 A, a PUPI for ISO 14443 B, an IDm for FeliCa, a UID
  // for ISO 15693.
  static const uint8_t GET_UID[] = {0xff, 0xca, 0x00, 0x00, 0x00};
  uint8_t response[sizeof(info.uid) + 2] = {};
  size_t responseLength = 0;
  uint16_t statusWord = 0;
  if (!ccidApdu(GET_UID,
                sizeof(GET_UID),
                response,
                sizeof(response),
                &responseLength,
                &statusWord,
                slot,
                address,
                timeoutMs))
  {
    // No identifier to go on; the ATR result stands.
    return true;
  }
  if (statusWord != 0x9000 || responseLength == 0 || responseLength > sizeof(info.uid))
  {
    return true;
  }

  memcpy(info.uid, response, responseLength);
  info.uidLength = static_cast<uint8_t>(responseLength);
  const EspUsbHostCcidCardStandard standard = espUsbHostCcidStandardFromUid(info.uid, info.uidLength);
  if (standard == ESP_USB_HOST_CCID_CARD_UNKNOWN)
  {
    return true;
  }
  info.standard = standard;
  info.standardText = espUsbHostCcidCardStandardText(standard);
  info.fromUid = true;
  return true;
}

bool EspUsbHost::ccidDataExchange(uint8_t messageType,
                                  const uint8_t *tx,
                                  size_t txLength,
                                  uint8_t *rx,
                                  size_t rxCapacity,
                                  size_t *rxLength,
                                  uint8_t slot,
                                  uint8_t address,
                                  uint32_t timeoutMs)
{
  if (rxLength)
  {
    *rxLength = 0;
  }
  if (txLength > 0 && !tx)
  {
    return false;
  }

  const uint8_t expectedResponse = messageType == CCID_PC_TO_RDR_ESCAPE
                                       ? CCID_RDR_TO_PC_ESCAPE
                                       : CCID_RDR_TO_PC_DATA_BLOCK;
  EspUsbHostCcidResponse response;
  if (!ccidMessage(messageType, nullptr, tx, txLength, response, slot, address, timeoutMs))
  {
    return false;
  }
  if (response.commandStatus != ESP_USB_HOST_CCID_COMMAND_OK ||
      response.messageType != expectedResponse)
  {
    ESP_LOGW(TAG, "CCID message 0x%02x failed slot=%u status=0x%02x error=0x%02x",
             messageType,
             slot,
             response.status,
             response.error);
    return false;
  }
  // Chained responses only occur at extended APDU exchange level, which this
  // API does not assemble. Reporting the failure beats returning a fragment.
  if (response.messageType == CCID_RDR_TO_PC_DATA_BLOCK && response.chainParameter != 0)
  {
    ESP_LOGW(TAG, "CCID chained response is not supported (bChainParameter=0x%02x)",
             response.chainParameter);
    return false;
  }
  if (rxLength)
  {
    *rxLength = response.length;
  }
  if (response.length > rxCapacity)
  {
    ESP_LOGW(TAG, "CCID response of %u bytes does not fit in %u bytes",
             static_cast<unsigned>(response.length),
             static_cast<unsigned>(rxCapacity));
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }
  if (response.length > 0 && rx)
  {
    memcpy(rx, response.data, response.length);
  }
  return true;
}

bool EspUsbHost::ccidTransfer(const uint8_t *tx,
                              size_t txLength,
                              uint8_t *rx,
                              size_t rxCapacity,
                              size_t *rxLength,
                              uint8_t slot,
                              uint8_t address,
                              uint32_t timeoutMs)
{
  return ccidDataExchange(CCID_PC_TO_RDR_XFR_BLOCK, tx, txLength, rx, rxCapacity, rxLength, slot, address, timeoutMs);
}

bool EspUsbHost::ccidEscape(const uint8_t *tx,
                            size_t txLength,
                            uint8_t *rx,
                            size_t rxCapacity,
                            size_t *rxLength,
                            uint8_t slot,
                            uint8_t address,
                            uint32_t timeoutMs)
{
  return ccidDataExchange(CCID_PC_TO_RDR_ESCAPE, tx, txLength, rx, rxCapacity, rxLength, slot, address, timeoutMs);
}

bool EspUsbHost::ccidApdu(const uint8_t *apdu,
                          size_t apduLength,
                          uint8_t *response,
                          size_t responseCapacity,
                          size_t *responseLength,
                          uint16_t *statusWord,
                          uint8_t slot,
                          uint8_t address,
                          uint32_t timeoutMs)
{
  if (responseLength)
  {
    *responseLength = 0;
  }
  if (statusWord)
  {
    *statusWord = 0;
  }
  if (!apdu || apduLength < 4)
  {
    return false;
  }

  // The exchange goes through ccidMessage() rather than ccidTransfer() so the
  // status word can be split off in place: the caller's buffer only has to hold
  // the data part, not the whole response.
  EspUsbHostCcidResponse apduResponse;
  if (!ccidMessage(CCID_PC_TO_RDR_XFR_BLOCK, nullptr, apdu, apduLength, apduResponse, slot, address, timeoutMs))
  {
    return false;
  }
  if (apduResponse.commandStatus != ESP_USB_HOST_CCID_COMMAND_OK ||
      apduResponse.messageType != CCID_RDR_TO_PC_DATA_BLOCK)
  {
    ESP_LOGW(TAG, "CCID APDU failed slot=%u status=0x%02x error=0x%02x",
             slot,
             apduResponse.status,
             apduResponse.error);
    return false;
  }
  if (apduResponse.chainParameter != 0)
  {
    ESP_LOGW(TAG, "CCID chained APDU response is not supported (bChainParameter=0x%02x)",
             apduResponse.chainParameter);
    return false;
  }
  if (apduResponse.length < 2)
  {
    ESP_LOGW(TAG, "CCID APDU response of %u bytes has no status word",
             static_cast<unsigned>(apduResponse.length));
    return false;
  }

  const uint8_t *payload = apduResponse.data;
  const size_t dataLength = apduResponse.length - 2;
  if (statusWord)
  {
    *statusWord = static_cast<uint16_t>((static_cast<uint16_t>(payload[dataLength]) << 8) |
                                        payload[dataLength + 1]);
  }
  if (responseLength)
  {
    *responseLength = dataLength;
  }
  if (dataLength > responseCapacity)
  {
    ESP_LOGW(TAG, "CCID APDU data of %u bytes does not fit in %u bytes",
             static_cast<unsigned>(dataLength),
             static_cast<unsigned>(responseCapacity));
    setLastError(ESP_ERR_INVALID_SIZE);
    return false;
  }
  if (dataLength > 0 && response)
  {
    memcpy(response, payload, dataLength);
  }
  return true;
}

bool EspUsbHost::ccidAbort(uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
  DeviceState *device = findCcidDevice(address);
  if (!device)
  {
    return false;
  }
  if (slot >= device->ccidSlotCount)
  {
    return false;
  }
  if (xTaskGetCurrentTaskHandle() == clientTaskHandle_)
  {
    ESP_LOGW(TAG, "ccidAbort() cannot run from USB client task");
    return false;
  }

  if (device->ccidLock && xSemaphoreTake(device->ccidLock, pdMS_TO_TICKS(timeoutMs)) != pdTRUE)
  {
    setLastError(ESP_ERR_TIMEOUT);
    return false;
  }

  bool ok = false;
  if (device->hasCcidInterface)
  {
    // The CCID spec pairs the class request with a PC_to_RDR_Abort carrying the
    // same bSlot / bSeq, so the sequence number is reserved before both.
    const uint8_t sequence = device->ccidSequence;
    ok = submitVendorControl(*device,
                             CCID_ABORT_REQUEST_TYPE,
                             CCID_CLASS_REQUEST_ABORT,
                             static_cast<uint16_t>(slot | (static_cast<uint16_t>(sequence) << 8)),
                             device->ccidInterfaceNumber,
                             nullptr,
                             0,
                             nullptr,
                             timeoutMs);
    if (ok)
    {
      EspUsbHostCcidResponse response;
      ok = ccidExchange(*device, CCID_PC_TO_RDR_ABORT, slot, nullptr, nullptr, 0, response, timeoutMs) &&
           response.messageType == CCID_RDR_TO_PC_SLOT_STATUS;
    }
  }

  if (device->ccidLock)
  {
    xSemaphoreGive(device->ccidLock);
  }
  return ok;
}

void EspUsbHost::onCcidCardInserted(CcidSlotChangeCallback callback)
{
  ccidCardInsertedCallback_ = callback;
}

void EspUsbHost::onCcidCardRemoved(CcidSlotChangeCallback callback)
{
  ccidCardRemovedCallback_ = callback;
}

void EspUsbHost::handleCcidNotification(DeviceState &device, const uint8_t *data, size_t length)
{
  if (!device.hasCcidInterface || !data || length < 1)
  {
    return;
  }

  if (data[0] == CCID_RDR_TO_PC_HARDWARE_ERROR)
  {
    ESP_LOGW(TAG, "CCID hardware error: slot=%u code=0x%02x",
             length > 1 ? data[1] : 0,
             length > 3 ? data[3] : 0);
    return;
  }
  if (data[0] != CCID_RDR_TO_PC_NOTIFY_SLOT_CHANGE || length < 2)
  {
    return;
  }

  // bmSlotICCState packs two bits per slot: bit 0 is the current state, bit 1
  // is set when the state changed since the previous notification.
  for (uint8_t slot = 0; slot < device.ccidSlotCount && slot < 8; slot++)
  {
    const size_t byteIndex = 1 + slot / 4;
    if (byteIndex >= length)
    {
      break;
    }
    // Bit 1 (changed) is not used: a change that ends in the state the slot was
    // already known to be in has nothing to report, and the first notification
    // after opening is reported whether or not it is flagged as a change.
    const uint8_t bits = static_cast<uint8_t>((data[byteIndex] >> ((slot % 4) * 2)) & 0x03);
    const bool present = (bits & 0x01) != 0;
    const uint8_t mask = static_cast<uint8_t>(1u << slot);
    const bool known = (device.ccidSlotKnownMask & mask) != 0;
    const bool wasPresent = (device.ccidSlotPresentMask & mask) != 0;

    device.ccidSlotKnownMask = static_cast<uint8_t>(device.ccidSlotKnownMask | mask);
    device.ccidSlotPresentMask = present
                                     ? static_cast<uint8_t>(device.ccidSlotPresentMask | mask)
                                     : static_cast<uint8_t>(device.ccidSlotPresentMask & ~mask);

    if (known && wasPresent == present)
    {
      continue;
    }
    if (!present && slot == device.ccidAtrSlot)
    {
      device.ccidAtrLength = 0;
    }

    EspUsbHostCcidSlotEvent event;
    event.address = device.info.address;
    event.slot = slot;
    event.present = present;
    ESP_LOGI(TAG, "CCID slot change: address=%u slot=%u present=%u",
             event.address,
             event.slot,
             event.present ? 1 : 0);
    if (present && ccidCardInsertedCallback_)
    {
      ccidCardInsertedCallback_(event);
    }
    else if (!present && ccidCardRemovedCallback_)
    {
      ccidCardRemovedCallback_(event);
    }
  }
}
