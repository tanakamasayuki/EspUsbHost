# USB Host Development Guide (Advanced)

> 日本語版: [usb-host-advanced.ja.md](usb-host-advanced.ja.md)

The sequel to the [USB Host Development Guide](usb-host-guide.md). Where the first guide covers getting a device to work, this one covers **why it behaves the way it does, where the limits are, and what to measure when you hit them**.

It is written for someone who has already got at least one device working and now faces one of these:

- Reading descriptors or captures byte by byte
- An endpoint that will not claim, throughput that is not enough, or transfers that stall
- Writing a wrapper for a class the library does not implement
- Needing to know exactly what may and may not be done inside a callback

## Contents

1. [Architecture and task model](#1-architecture-and-task-model)
2. [Reading descriptors byte by byte](#2-reading-descriptors-byte-by-byte)
3. [Anatomy of a control transfer](#3-anatomy-of-a-control-transfer)
4. [Transfer timing and bandwidth](#4-transfer-timing-and-bandwidth)
5. [Endpoint resources: channels and FIFO](#5-endpoint-resources-channels-and-fifo)
6. [Errors and recovery](#6-errors-and-recovery)
7. [Designing for throughput](#7-designing-for-throughput)
8. [Callback context](#8-callback-context)
9. [Implementing a new class or protocol](#9-implementing-a-new-class-or-protocol)
10. [Measuring and debugging](#10-measuring-and-debugging)

---

## 1. Architecture and task model

### 1.1 The layers

```
Sketch (setup / loop)
  |  callbacks and send APIs
EspUsbHost               ... class drivers, device state, receive buffers
  |  usb_host_* API
ESP-IDF USB Host Library ... daemon, client, enumeration, external hub driver
  |  HCD
HCD (DWC OTG)            ... channel allocation, FIFO, URB scheduling
  |
USB OTG controller + PHY
```

The constraints that "cannot be changed from Arduino" ([guide 3.3](usb-host-guide.md#33-limits-that-come-from-the-arduino-build-configuration)) come from the bottom two layers being a precompiled Arduino-ESP32 binary. The top two are source you can change.

### 1.2 Two tasks

`begin()` creates **two FreeRTOS tasks**.

| Task | Name | Role |
|------|------|------|
| Daemon | `EspUsbHost` | Runs `usb_host_lib_handle_events()`: library install, enumeration, device creation and teardown |
| Client | `EspUsbHostClient` | Runs `usb_host_client_handle_events()` with a 5 ms timeout. **Transfer completions and all of this library's callbacks run here** |

Both are created with `EspUsbHostConfig`'s `taskStackSize` (default 8192), `taskPriority` (default 5) and `taskCore` (default `tskNO_AFFINITY`).

```cpp
EspUsbHostConfig config;
config.taskStackSize = 12288;   // raise it if callbacks do real work
config.taskPriority  = 6;       // raise it if events are being missed
config.taskCore      = 1;       // pin it away from Wi-Fi
usb.begin(config);
```

Besides events, the client task does the following on every pass. Knowing this tells you where latency comes from:

- Finalising disconnected devices (`disconnectPending`)
- Sending keyboard LED reports, coalescing rapid changes by waiting **20 ms** after the last one
- Recovering endpoints stopped by an error ([chapter 6](#6-errors-and-recovery))
- Resubmitting IN transfers

So the **loop period is at best 5 ms**, and blocking inside a callback stops all of it.

### 1.3 IN transfers stay submitted

For interrupt IN endpoints (keyboards, mice, CCID notifications) the library keeps one URB outstanding at all times: complete, handle, resubmit immediately. `managedEndpointCount()` counts the endpoints with such a permanent receive transfer.

Bulk IN has two modes instead (`vendorOpen()`'s `readMode`):

| Mode | Behaviour | Suits |
|------|-----------|-------|
| `ESP_USB_HOST_VENDOR_READ_CONTINUOUS` | Keeps an IN transfer outstanding and buffers what arrives (512 bytes by default, `ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE`) | Devices that stream unprompted |
| `ESP_USB_HOST_VENDOR_READ_ON_DEMAND` | Transfers only when `vendorReadSync()` asks | Request/response protocols; devices such as BOT where an IN outside a transaction is a transfer error |

Getting this wrong shows up as timeouts, or a stream of transfer errors in the log.

### 1.4 Intervening in enumeration

`setConfigurationSelector()` uses ESP-IDF's `enum_filter_cb` to **choose which configuration is activated during enumeration**. It is needed by USB Ethernet adapters and similar devices that hide the interesting function outside the default configuration.

- Register it before `begin()`
- It runs on the USB Host task and **must not block**
- Needs arduino-esp32 3.3.11 or later (otherwise it returns `false` with `ESP_ERR_NOT_SUPPORTED`)
- Only one instance in the process can own it (a second one gets `ESP_ERR_INVALID_STATE`)

Returning `0` keeps the device default. To find out what is in which configuration, enumerate once with the default and inspect all configurations with [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) or [`usb_network_descriptor`](../tests/manual/usb_network_descriptor/) — in practice a two-pass enumeration.

### 1.5 Shutdown

`end()` unmounts any MSC volume this instance mounted, stops the client and daemon synchronously, cancels and drains in-flight transfers, deregisters the client, waits for the IDF `ALL_FREE` handshake, and uninstalls the host library. The uninstall is retried while library events are handled until the IDF accepts it, because it refuses while an event flag such as `NO_CLIENTS` is still unread — with an empty device list nothing else would consume it. The same object can then be started again with `begin()`. **Never call it from a USB callback** — call it from the application task.

---

## 2. Reading descriptors byte by byte

Tables for reading the raw bytes that [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) and [`raw_descriptor`](../tests/manual/raw_descriptor/) print. All multi-byte USB values are **little-endian**.

### 2.1 Device descriptor (18 bytes)

| Offset | Size | Field | Meaning |
|--------|------|-------|---------|
| 0 | 1 | bLength | 18 |
| 1 | 1 | bDescriptorType | 0x01 |
| 2 | 2 | bcdUSB | USB version (0x0200 = USB 2.0) |
| 4 | 1 | bDeviceClass | 0x00 means "see the interfaces" |
| 5 | 1 | bDeviceSubClass | |
| 6 | 1 | bDeviceProtocol | |
| 7 | 1 | bMaxPacketSize0 | EP0 packet size (LS: 8 / FS: 8,16,32,64 / HS: 64) |
| 8 | 2 | idVendor | VID |
| 10 | 2 | idProduct | PID |
| 12 | 2 | bcdDevice | Device revision — useful for telling firmware versions apart |
| 14 | 1 | iManufacturer | String index |
| 15 | 1 | iProduct | String index |
| 16 | 1 | iSerialNumber | String index |
| 17 | 1 | bNumConfigurations | **Anything above 1 deserves attention** |

### 2.2 Configuration descriptor (9 bytes plus what follows)

| Offset | Size | Field | Meaning |
|--------|------|-------|---------|
| 0 | 1 | bLength | 9 |
| 1 | 1 | bDescriptorType | 0x02 |
| 2 | 2 | wTotalLength | **Total bytes including everything that follows. Over 256 and the device cannot enumerate on an ESP32** |
| 4 | 1 | bNumInterfaces | |
| 5 | 1 | bConfigurationValue | The value passed to SET_CONFIGURATION |
| 6 | 1 | iConfiguration | |
| 7 | 1 | bmAttributes | bit6 = self-powered, bit5 = remote wakeup |
| 8 | 1 | bMaxPower | **In 2 mA units** (0x32 = 100 mA) |

Interface, endpoint and class-specific descriptors follow **concatenated** after these 9 bytes. Walking them is nothing more than "first byte is the length, second byte is the type", repeatedly.

### 2.3 Interface descriptor (9 bytes)

| Offset | Field | Notes |
|--------|-------|-------|
| 2 | bInterfaceNumber | The number passed to `vendorOpen()` |
| 3 | bAlternateSetting | **Anything but 0 has to be selected with SET_INTERFACE** |
| 4 | bNumEndpoints | Can be 0 at alt=0 (typical for audio) |
| 5–7 | bInterfaceClass / SubClass / Protocol | What it is |

Audio and video put the bandwidth-consuming setting in `bAlternateSetting >= 1`, with no endpoints at alt=0. That is how a device avoids reserving bandwidth while idle.

### 2.4 Endpoint descriptor (7 bytes)

| Offset | Field | Notes |
|--------|-------|-------|
| 2 | bEndpointAddress | bit7 = direction (1 = IN), bits 3:0 = number |
| 3 | bmAttributes | bits 1:0 = 0 control, 1 iso, 2 bulk, 3 interrupt. For iso, bits 3:2 are the sync type and 5:4 the usage type |
| 4 | wMaxPacketSize | **bits 10:0 are the size; bits 12:11 are additional transactions per microframe** (high-bandwidth HS iso/interrupt) |
| 6 | bInterval | Polling interval; the meaning depends on the speed ([4.2](#42-how-to-read-binterval)) |

Reading `0x0400` as simply 1024 is not always enough: a high-bandwidth iso endpoint runs `(1 + bits 12:11)` transactions per microframe.

### 2.5 Class-specific descriptors

Any block with `bDescriptorType` at 0x21 or above is defined by that interface's class. A parsed dump does not show them, so the raw bytes are the only way in.

| Type | Examples |
|------|----------|
| 0x21 | HID descriptor (which carries the report descriptor's length and type); a different meaning for CDC and printers |
| 0x22 | HID report descriptor (fetched separately with GET_DESCRIPTOR) |
| 0x24 | CS_INTERFACE: CDC functional descriptors, UAC units, the CCID class descriptor |
| 0x25 | CS_ENDPOINT: UAC endpoint attributes and similar |
| 0x0b | IAD, grouping several interfaces into one function |
| 0x29 | Hub descriptor |

### 2.6 String descriptors

Strings are referenced **by index**; the content is a separate request. `GET_DESCRIPTOR(type=0x03, index=0)` returns the list of language IDs (usually 0x0409, en-US), and `index=n, wIndex=langid` returns the string itself, in UTF-16LE.

From the console:

```
ctl 80 06 0300 0000 ff      # language ID list
ctl 80 06 0302 0409 ff      # the string at iProduct=2
```

### 2.7 HID report descriptors

HID is the one class that declares the *meaning* of its data in a separate descriptor. It is a list of items whose first byte is `bTag (bits 7:4) | bType (bits 3:2) | bSize (bits 1:0)` (sizes 0, 1, 2 or 4 bytes).

| Kind | Main items | Role |
|------|-----------|------|
| Global | Usage Page, Report Size, Report Count, Report ID, Logical Min/Max | Settings that apply from here on |
| Local | Usage, Usage Min/Max | Apply only to the next Main item |
| Main | Input, Output, Feature, Collection, End Collection | Define actual fields |

The key to reading it is that **Report Size × Report Count is the number of bits that Main item occupies**. If a Report ID is declared, the first byte of every report is that ID. Line this up against the raw bytes from `onHIDInput()` and you can pin down what each bit means.

A Usage Page of `0xff00` or above (vendor-defined) means the content is a private protocol — continue with [Step 5 of the first guide](usb-host-guide.md#step-5-look-for-published-information). [`EspUsbHostHIDReportDescriptor`](../examples/Info/EspUsbHostHIDReportDescriptor/) fetches and decodes it; `espUsbHostPrintHIDReportDescriptor()` formats raw bytes.

---

## 3. Anatomy of a control transfer

### 3.1 The 8-byte setup packet

Every control transfer starts with these 8 bytes, and they map one-to-one onto the console's `ctl` arguments.

| Byte | Field | Content |
|------|-------|---------|
| 0 | bmRequestType | bit7 = direction (1 = IN) / bits 6:5 = type (0 standard, 1 class, 2 vendor) / bits 4:0 = recipient (0 device, 1 interface, 2 endpoint) |
| 1 | bRequest | Request number |
| 2–3 | wValue | Request-specific |
| 4–5 | wIndex | Often an interface number or an endpoint address |
| 6–7 | wLength | Data stage length |

Common `bmRequestType` values:

| Value | Meaning |
|-------|---------|
| `0x80` | Standard, device, IN (GET_DESCRIPTOR) |
| `0x00` | Standard, device, OUT (SET_CONFIGURATION) |
| `0x02` | Standard, endpoint, OUT (CLEAR_FEATURE to clear a halt) |
| `0x21` / `0xa1` | Class, interface, OUT / IN (HID SET_REPORT, USBTMC, CCID) |
| `0x40` / `0xc0` | Vendor, device, OUT / IN |

Standard request numbers: `GET_STATUS=0`, `CLEAR_FEATURE=1`, `SET_FEATURE=3`, `SET_ADDRESS=5`, `GET_DESCRIPTOR=6`, `GET_CONFIGURATION=8`, `SET_CONFIGURATION=9`, `GET_INTERFACE=10`, `SET_INTERFACE=11`.

### 3.2 The three stages

A control transfer is Setup → (Data) → Status. A device that cannot accept the request answers with a **STALL**, which surfaces as a transfer error. **A STALL is not a fault; it is the answer "I do not support that request."** When `ctl` fails, suspect the recipient (device vs interface) and `wIndex` first.

### 3.3 EP0 belongs to the device

EP0 belongs to the device, not to an interface, which is why `vendorControlTransfer()` works without `vendorOpen()` (internally it falls back to a plain device lookup when no vendor device matches). EP0 requests can be sent even to a device whose interfaces the library has claimed.

### 3.4 The 256-byte wall (again)

`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256` caps one transfer at 256 bytes including the 8-byte setup packet. The implementation calls `usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, ...)`, so **248 bytes of data is the ceiling**. Long descriptors can be fetched in pieces — but **the enumeration path inside IDF reads the configuration in one go**, so a device with `wTotalLength > 256` never enumerates in the first place.

---

## 4. Transfer timing and bandwidth

### 4.1 Frames and microframes

| Speed | Unit | Length |
|-------|------|--------|
| LS / FS | Frame | 1 ms |
| HS | Microframe | 125 µs |

The host divides the bus into these units, schedules periodic transfers (interrupt and iso) first, and gives what is left to bulk. Periodic transfers may reserve at most 90 % of a frame.

### 4.2 How to read bInterval

| Speed and type | Meaning |
|----------------|---------|
| FS interrupt | Milliseconds, directly (1–255) |
| LS interrupt | 10–255 ms |
| FS iso | `2^(bInterval-1)` frames (normally 1) |
| HS interrupt / iso | `2^(bInterval-1)` microframes (bInterval=4 → 8 × 125 µs = 1 ms) |

This is the first thing to check when polling feels slow. But **bInterval is a request, not a guarantee**: the actual interval depends on host scheduling and channel contention.

### 4.3 Max packet size constraints

| Transfer | LS | FS | HS |
|----------|----|----|----|
| Control | 8 | 8/16/32/64 | 64 |
| Bulk | — | 8/16/32/64 | **512, fixed** |
| Interrupt | ≤8 | ≤64 | ≤1024 |
| Isochronous | — | ≤1023 | ≤1024 (×3 high-bandwidth) |

A 1024-byte interrupt OUT cannot be opened on a full-speed port because full speed has no such size to begin with ([guide 3.2](usb-host-guide.md#32-choosing-the-fs-or-the-hs-port-p4)).

### 4.4 Theory versus measurement

| | Theoretical ceiling | Measured here (bulk OUT) |
|---|---------------------|--------------------------|
| FS | 19 packets × 64 B per frame ≈ 1.216 MB/s | **1.098 MB/s** (ESP32-S3, async queue depth 2) |
| HS | 13 transactions × 512 B per microframe ≈ 53 MB/s | **36.4 MB/s** (ESP32-P4, async queue depth 2, 8 KB transfers) |

The measurements come from [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/). The gap is host-side URB handling, the gaps between transfers, and what the device can absorb. **Budget against the measured number, not the theoretical one.** Pushing a 320×240 16 bpp screen over full speed means 153,600 bytes per frame, so 1.098 MB/s caps you at roughly 7 fps.

---

## 5. Endpoint resources: channels and FIFO

There are two independent reasons an endpoint fails to claim — **no channel** and **no FIFO** — and they can both surface as `ESP_ERR_NOT_SUPPORTED`, so they have to be told apart.

### 5.1 Channel accounting

The library's estimate (`estimatedHcdChannelCount()`) is:

```
EP0 (one per device) + claimed endpoints + hub endpoints
```

`maxEndpointChannelCount()` returns 8 (the ESP32-S3's `OTG_NUM_HOST_CHAN`). It is a diagnostic estimate rather than a guaranteed match for what the HCD allocates, but it predicts exhaustion well enough to design against.

| API | Meaning |
|-----|---------|
| `endpointChannelCount()` | Endpoints of claimed interfaces |
| `managedEndpointCount()` | Endpoints with a permanent receive transfer |
| `ep0ChannelCount()` | Tracked devices (i.e. EP0s) |
| `hubEndpointChannelCount()` | Endpoints belonging to hubs |
| `estimatedHcdChannelCount()` | The sum of the above |

Exhaustion in the log:

```
No more HCD channels available
EP Alloc error: ESP_ERR_NOT_SUPPORTED
Claiming interface error: ESP_ERR_NOT_SUPPORTED
```

The fixes are: fewer devices, avoiding composite devices with interfaces you do not need, or `setHubTrackingEnabled(false)` to stop tracking hubs (the hub information APIs stop working, but devices behind the hub still do).

### 5.2 The FIFO split

The host controller splits its hardware FIFO three ways, and that split decides **the largest endpoint MPS that can be opened**. The unit is a 4-byte "line".

| Target | Limit |
|--------|-------|
| IN (any type) | `(rxFifoLines - 2) * 4` |
| Control / bulk OUT | `nptxFifoLines * 4` |
| Interrupt / iso OUT | `ptxFifoLines * 4` |

The high-speed default is rx = total − 384, nptx = 256, ptx = 128 lines, which leaves **only 512 bytes for periodic OUT**. A device with a 1024-byte interrupt OUT (an HS vendor HID panel, for example) fails there, and the driver logs:

```
HCD DWC: EP MPS (1024) exceeds supported limit (512)
```

Repartition to fix it:

```cpp
EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
config.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // {260, 128, 280} lines
usb.begin(config);
```

Constraints:

- The total must fit: 1024 lines (4 KB) on the P4 high-speed port, 256 lines (1 KB) on a full-speed port
- `rxFifoLines` and `nptxFifoLines` cannot be 0 (that would leave control transfers with no FIFO)
- Exceeding the total is rejected by `begin()` with `ESP_ERR_INVALID_SIZE` / `ESP_ERR_INVALID_ARG`
- Needs arduino-esp32 3.3.0 or later; older cores warn and use the default

When set, the startup log states the resulting limits, so a failed claim can be compared against it directly:

```
FIFO lines rx=260 nptx=128 ptx=280 (total=668) -> max MPS in=1032 bulk_out=512 periodic_out=1120
```

A full-speed port only has 256 lines in total, so **no split makes a 1024-byte endpoint openable there.**

---

## 6. Errors and recovery

### 6.1 NAK and STALL are different

- **NAK** — "nothing ready right now". The host retries; it is not an error, and polling produces it constantly.
- **STALL** — "I cannot handle that request or transfer". The endpoint halts and stays halted until `CLEAR_FEATURE(ENDPOINT_HALT)` (EP0 recovers on the next setup packet).

### 6.2 What the library does

When an IN transfer ends in an error, the library does not resubmit immediately. It sets `recoveryPending`, and on the client task's next pass it calls `usb_host_endpoint_clear()` (clearing the halt) and only then resubmits. The ordering matters because **endpoint callbacks are dispatched before `DEV_GONE` events**, and this prevents a new URB being sent to a device that is already gone.

So one or two error lines right after unplugging, followed by the disconnect being processed, is the normal sequence.

Bulk OUT behaves differently. A transfer error halts the pipe and ESP-IDF then refuses every submit until it is cleared; the library **clears it automatically on the next write, on the calling task**, because clearing can block and must not happen in a completion callback.

There is one exception: **a write issued from the USB client task (i.e. inside a callback) cannot recover.**

```
vendor bulk OUT halted; cannot recover from the USB client task
```

The call returns `false` and `lastError()` is `ESP_ERR_INVALID_STATE`. If you send from callbacks, a single error leaves the write path wedged — one more reason for [chapter 8](#8-callback-context). To clear it by hand from the console:

```
ctl 02 01 0000 0001 0      # CLEAR_FEATURE(ENDPOINT_HALT) on EP 0x01
```

### 6.3 How a transfer ends

A bulk transfer ends at the first packet **shorter than the max packet size**. When the length is an exact multiple of MPS, the receiver assumes more is coming, so some protocols require a **ZLP (zero-length packet)**.

- One at a time: `vendorWriteZlp()`
- Automatic: `vendorSetAutoZlp(true)` (with the async queue this consumes a second slot, so use depth ≥ 2)

ADB and CDC-NCM need this. When "only large payloads stall" or "it hangs at one particular size", check this first.

### 6.4 Timeouts

Synchronous APIs (`vendorControlTransfer()`, `vendorReadSync()`, MSC, CCID) have default timeouts — 1000 ms for control, 5000 ms for MSC and CCID — and leave `ESP_ERR_TIMEOUT` in `lastError()`. Rather than raising the value, ask **why there is no answer**: wrong read mode, an unsupported request, or a halted endpoint.

---

## 7. Designing for throughput

### 7.1 The limit of synchronous writes

`vendorWrite()` and `sendSerial()` wait for each transfer to complete, leaving the bus idle in between, so the smaller the transfer the worse the efficiency. The measurements show exactly that: the smaller the transfer, the bigger the gap to the async queue.

### 7.2 The asynchronous queue

```cpp
usb.vendorWriteQueueBegin(2 /* depth */, 8192 /* bytes per slot */);

size_t capacity = 0;
uint8_t *buffer = usb.vendorWriteAcquire(&capacity, 100 /* ms */);
if (buffer) {
  size_t n = encodeInto(buffer, capacity);   // zero-copy: write straight into the DMA buffer
  usb.vendorWriteSubmit(buffer, n);
}
```

Points that matter:

- **Depth 2 is usually enough.** The measured peaks at both FS and HS come from depth 2. Deeper costs DMA memory (the cap is 8)
- `vendorWriteAcquire()` → `vendorWriteSubmit()` is zero-copy; `vendorWriteAsync()` is the copying convenience version
- The queue **is** the backpressure: with no free slot, `vendorWriteAcquire()` waits, so a producer faster than the bus cannot grow the in-flight set until DMA memory runs out
- `vendorWriteStats()` reports `submitted / completed / errors / queueFullEvents / zlp / bytes`. A high `queueFullEvents` means the producer is outrunning the bus — you are at the ceiling
- The async calls never wait for completion, so **they may be used from USB callbacks**

The CDC serial path has the same queue (`serialWriteQueueBegin()`). There it also solves a second problem: without it, `sendSerial()` allocates a transfer per call with no backpressure at all.

### 7.3 The receive side

- The vendor bulk IN buffer is 512 bytes by default (`ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE`, a build flag)
- The CDC receive ring is 512 bytes by default and can be changed **at runtime with `setRxBufferSize()`** — the preferred way
- Overflow drops the oldest bytes with no error, so raise it if you lose data at high baud rates

### 7.4 Memory and cache

Transfer buffers come from DMA-capable memory (`usb_host_transfer_alloc`). On the ESP32-P4 that memory is cached, so the library writes the cache lines back with `esp_cache_msync()` immediately before every IN submit. No application-side workaround is needed (verified by [`msc_cache_coherency`](../tests/manual/msc_cache_coherency/)).

---

## 8. Callback context

**Every callback runs on the USB client task**, not in `loop()`. The rules follow from that:

| Allowed | Not allowed |
|---------|-------------|
| Copy data, set a flag, push to a queue | Synchronous APIs that wait for completion (`vendorWrite`, `vendorReadSync`, `vendorControlTransfer`, MSC, CCID, `end()`) |
| `vendorWriteSubmit()` on the async queue | `delay()` or long loops |
| Short log output | Large heap allocations, file I/O, network work |

The synchronous APIs detect the client task themselves and return `false` — waiting there could never make progress, because the completion they wait for is dispatched by the very task that is blocked. **"It stopped working when I sent from the callback" is almost always this.** The correct shape:

```cpp
volatile bool sendRequested = false;

usb.onVendorData([](const EspUsbHostVendorData &data) {
  // data.data is valid only during the callback; copy what you need
  memcpy(rxBuffer, data.data, min(data.length, sizeof(rxBuffer)));
  sendRequested = true;          // do the send in loop()
});

void loop() {
  if (sendRequested) {
    sendRequested = false;
    usb.vendorWrite(payload, sizeof(payload));
  }
}
```

**Pointer lifetime** is the other shared rule. The pointers handed to `onVendorData`, `onHIDInput`, `onNetworkFrame` and friends point into the library's own buffers and are reused once the callback returns. Copy anything you keep.

Listeners (`addKeyboardListener()` and friends) run after the `on*()` callback, in registration order. The callback set is snapshotted per event, so **adding or removing a listener inside a callback takes effect on the next event**. The default is four per event (`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`).

---

## 9. Implementing a new class or protocol

Almost any unsupported class can be handled as "open the interface with the generic API and speak the protocol yourself". The printer, USBTMC, ADB, DisplayLink, AX206 and DP100 support in this repository is all built that way.

### 9.1 Opening the interface

```cpp
// Any class, selected by number
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
```

By default `vendorOpen()` picks the first `0xff` interface, but **naming a number claims it whatever its class** — which is how printers (0x07) and USBTMC (0xfe) work here. Interfaces the library itself has claimed (HID, CDC, MSC, ...) are still refused.

After opening:

```cpp
usb.vendorOutEndpoint(address);   // which bulk OUT was selected
usb.vendorInEndpoint(address);
usb.vendorOutPacketSize(address); // MPS: needed for ZLP decisions and splitting
```

When an interface exposes several bulk endpoints per direction, always check which one was chosen.

### 9.2 Class requests go on EP0

Most classes wrap their bulk traffic in EP0 class requests.

```cpp
// USBTMC GET_CAPABILITIES (class, interface, IN)
usb.vendorControlTransfer(0xa1, 7, 0, interfaceNumber, buf, sizeof(buf), &actual, address);

// Printer SOFT_RESET (class, interface, OUT)
usb.vendorControlTransfer(0x21, 2, 0, interfaceNumber, nullptr, 0, nullptr, address);
```

Forgetting the interface number in `wIndex` makes most devices stall.

### 9.3 The order to build it in

1. **Establish a round trip with read-only requests** (identity, status). If that works, the recipient and endpoint choices are right
2. **Reproduce the init sequence completely** — look for a request in the capture you skipped
3. **Make it a state machine**: explicit request/response pairing, timeouts, retries
4. **Add the write path**, with anything physically consequential last
5. **Exercise the error paths** (unplug, halt, timeout). That is what breaks first in real use

### 9.4 Implementations to copy from

| Goal | Reference |
|------|-----------|
| Minimal bulk round trip | [`EspUsbHostVendorBulk`](../examples/Vendor/EspUsbHostVendorBulk/) |
| Class requests plus a bulk message layer | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/) (class 0xfe) |
| Class requests, status polling, one large transfer | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/) (class 0x07) |
| A private framed protocol inside HID, with a CRC | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) |
| Authentication and multiplexed streams | [`EspUsbHostAdbConnect`](../examples/Vendor/EspUsbHostAdbConnect/) |
| High throughput with the async queue | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) |
| A device that needs a session kept alive | [`EspUsbHostMacroPadN3`](../examples/HID/EspUsbHostMacroPadN3/) |

---

## 10. Measuring and debugging

### 10.1 Reading the log

With Core Debug Level at `Verbose`, the ESP-IDF host stack reports enumeration and channel allocation in detail. Lines worth knowing:

| Log | Meaning |
|-----|---------|
| `No more HCD channels available` | Channel exhaustion ([5.1](#51-channel-accounting)) |
| `EP MPS (n) exceeds supported limit (m)` | FIFO split too small ([5.2](#52-the-fifo-split)) |
| `Enqueue URB error: ESP_ERR_INVALID_STATE` | A transfer right after disconnect. One or two lines on unplug is normal |
| An assert at `device_release ... ext_hub.c` | The IDF hub driver crashing; check the known combinations |
| `FIFO lines rx=... -> max MPS ...` | The effective limits of the split you configured |

### 10.2 What to measure with what

| Question | Tool |
|----------|------|
| Layout, classes, endpoints | [`EspUsbHostDeviceExplorer`](../examples/Info/EspUsbHostDeviceExplorer/) |
| Compare against raw descriptors | [`raw_descriptor`](../tests/manual/raw_descriptor/) plus `lsusb -v` on a PC |
| Try an arbitrary transfer | [`EspUsbHostProtocolConsole`](../examples/Vendor/EspUsbHostProtocolConsole/) |
| Channel usage | `estimatedHcdChannelCount()` / `printAllDeviceInfo()` |
| Effective bulk speed | [`vendor_bulk_throughput`](../tests/manual/vendor_bulk_throughput/) |
| Hot-plug robustness | [`hotplug`](../tests/manual/hotplug/) |
| Whether a hub is the cause | [`tests/probe/hub_enum`](../tests/probe/) |
| Identifying P4 ports | `p4_hs_host` / `p4_fs_host` / `p4_cdc` in [`tests/probe/`](../tests/probe/) |

`tests/probe/` is not a regression suite — it is **the place for throwaway bring-up and protocol-investigation sketches**. Adding a new investigation there is the established practice in this repository.

### 10.3 Principles for isolating a problem

1. **Try the same thing on a PC.** If it fails there too, it is not the ESP32
2. **Compare direct against through-a-hub.** A difference means channels, power, or that specific hub
3. **Swap the device.** If a keyboard works, the host side is fine
4. **Change the speed (P4).** Working at FS but not HS (or the reverse) points at MPS, hubs, or bandwidth
5. **Change one thing at a time.** FIFO settings, queue depth and task priority should never move together

---

## Related documents

- [USB Host Development Guide](usb-host-guide.md) — fundamentals, power, the experiment order, capturing
- [Tested Devices and Boards](tested-devices.md) — verified units and their conditions
- [README.md](../README.md) — API reference and per-class status
- [tests/manual/README.md](../tests/manual/README.md) — the manual test catalog and known issues
- [tests/TEST_PLAN.md](../tests/TEST_PLAN.md) — test strategy
- Per-protocol notes: [printer-spec.ja.md](printer-spec.ja.md) / [usbtmc-spec.ja.md](usbtmc-spec.ja.md) / [ccid-api-spec.ja.md](ccid-api-spec.ja.md) / [dp100-spec.ja.md](dp100-spec.ja.md) / [usb-display-spec.ja.md](usb-display-spec.ja.md) / [usb-network-spec.ja.md](usb-network-spec.ja.md) / [vendor-api-spec.ja.md](vendor-api-spec.ja.md)
