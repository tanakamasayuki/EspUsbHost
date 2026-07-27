# EspUsbHost UsbNetwork

> 日本語版: [README.ja.md](README.ja.md)

> ⚠️ **Experimental.** Arduino-ESP32 3.3.11 and later can select the active USB
> configuration during enumeration. This example selects CDC-NCM configuration
> 2 for the AX88179A (`0b95:1790`). For another adapter, just plug it in: this
> sketch prints the candidate list and the selector line to add on connect —
> paste it in and reset the board (see "Why Two Passes Are Needed").

Turns the board into a USB *host* for a USB Ethernet adapter (CDC-NCM / CDC-ECM)
and brings it up as an lwIP network interface. Plug in a USB NIC — or a second
board running the sibling
[EspUsbDevice `UsbNetwork`](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/UsbNetwork) sketch —
and standard Arduino networking (`NetworkClient`, `HTTPClient`) runs over USB,
with no Wi-Fi.

This is the counterpart to the EspUsbDevice `UsbNetwork` example: that sketch is
the network *device* (with its own DHCP server at `192.168.7.1`); this sketch is
the network *host* that receives a `192.168.7.x` lease and can reach it.

## Hardware

- ESP32-S3 (or another Arduino-ESP32 board with USB host support) as the host
- A CDC-NCM/ECM USB Ethernet adapter, or a second ESP32-S3 board running the
  EspUsbDevice `UsbNetwork` sketch
- A separate Serial monitor connection for logs

## What It Does

- Enumerates the USB device and, if it exposes a CDC-NCM/ECM interface, attaches
  it as a DHCP-client lwIP netif with `networkAttachNetif()`
- Prints, on connect, every CDC-ECM / CDC-NCM candidate found in *any*
  configuration along with the active configuration value, so the selector rule
  for an unknown adapter can be read straight off the serial log
- Prints the acquired IP address
- Optionally performs an `HTTPClient` GET over USB when `HTTP_TEST_URL` is set

## Key APIs

- `usb.networkAttachNetif(cfg, address)` opens the network interface (if needed)
  and registers it as an `esp_netif` netif. `EspUsbHostNetworkConfig` defaults to
  a DHCP client; set `dhcpClient=false` and fill `ip`/`gateway`/`subnet`(`/dns1`)
  for a static address.
- `usb.setConfigurationSelector(callback)` is registered before `usb.begin()`
  and returns the configuration value for a device descriptor. Return `0` to
  keep the device default. It runs in the USB Host task and must not block.
- `usb.networkLocalIP(address)` reports the interface address once leased.
- `usb.networkDetachNetif(address)` tears the netif down (also done automatically
  on USB disconnect).
- For raw Ethernet frames instead of an IP stack, use `usb.onNetworkFrame()` /
  `usb.networkWriteFrame()` / `usb.networkReadFrame()` and do not attach a netif.

## Why Two Passes Are Needed

An adapter whose CDC-NCM/ECM function is *not* in its default configuration
cannot be brought up in a single enumeration:

1. `setConfigurationSelector()` is driven by the USB Host Library's
   `enum_filter_cb`, which is called with **only the device descriptor**. No
   device handle exists yet, so the configuration descriptors — the only place
   the CDC-NCM/ECM interfaces are visible — cannot be read from inside the
   selector. The number must already be known when the selector returns.
2. Reading them requires an enumerated device: `usb.getNetworkInterfaces()`
   fetches config `1..bNumConfigurations` with `usb_host_get_config_desc()` and
   reports each candidate's `configurationValue`. This is what the sketch prints
   on connect — but by then the device is already running some other
   configuration.
3. `networkOpen()` / `networkAttachNetif()` only accept a candidate whose
   `configurationValue` equals the **active** configuration (an interface in a
   non-active configuration cannot be claimed). So the discovered value only
   takes effect on the *next* enumeration.

Hence: pass 1 enumerates with the default configuration and discovers the value;
pass 2 enumerates again with the selector returning it. In this sketch pass 2 is
the manual board reset after you add the printed selector rule.

### It is an ESP-IDF API constraint, not a USB one

This has nothing to do with TinyUSB (that is the device side; the host side here
is the ESP-IDF USB Host Library), and the USB specification does not require two
passes either. `GET_DESCRIPTOR(CONFIGURATION, index)` is a standard request that
a device must answer for **every** index while in the Address state, so
descriptors of non-active configurations are perfectly readable — which is why
`getNetworkInterfaces()` can report a CDC-NCM interface in configuration 2 while
configuration 1 is active. What the USB specification does say is only that one
configuration is active at a time, and that an interface can be claimed only in
the active one.

The two passes come from the ESP-IDF host API surface (checked against the IDF
headers bundled with the Arduino-ESP32 core):

- `enum_filter_cb` is `bool (*)(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue)`
  (`usb/usb_types_stack.h`), and its documentation states it must be
  non-blocking and **must not submit any USB transfers** — so a selector cannot
  fetch configuration descriptors itself.
- `usb_host_get_config_desc()` (`usb/usb_host.h`) needs a client handle and a
  device handle, i.e. it only works after enumeration has finished and the
  configuration is already chosen.
- `usb_host.h` exposes no way to change the configuration of an enumerated device
  (no `set_configuration`, no re-enumerate). Issuing SET_CONFIGURATION on EP0
  manually would desynchronize the stack's claimed interfaces and pipes.

If the filter callback were handed the configuration descriptors as well, one
pass would be enough; until then, discovering the value and using it necessarily
happen in different enumerations.

## Automating the Second Pass

The second pass can be triggered from the sketch by restarting the USB host
stack, which re-enumerates every device. Do it from `loop()` — `end()` refuses
to run on a USB Host task — and in this order:

```cpp
static uint16_t forcedVid = 0;
static uint16_t forcedPid = 0;
static uint8_t forcedConfiguration = 0;   // read by the selector on pass 2

// setup(): registered once, before begin(). end() keeps it, so it does not have
// to be re-registered on restart.
usb.setConfigurationSelector([](const usb_device_desc_t &device) -> uint8_t {
  if (forcedConfiguration && device.idVendor == forcedVid && device.idProduct == forcedPid) {
    return forcedConfiguration;
  }
  return 0;
});

// loop(): after the candidate scan found a complete candidate in another
// configuration (and only if no value is latched yet, so a wrong guess cannot
// restart forever).
if (!forcedConfiguration && candidate.configurationValue != nicConfiguration) {
  forcedVid = nicVid;
  forcedPid = nicPid;
  forcedConfiguration = candidate.configurationValue;

  usb.networkDetachNetif(nicAddress);  // REQUIRED before end(): the netif uses the
                                       // fixed if_key "USB_NCM" and the end() path
                                       // does not destroy it, so esp_netif_new()
                                       // would fail on the next attach
  usb.end();                           // ~up to 3 s; drains transfers, closes devices
  nicAddress = 0;                      // end() does not fire onDeviceDisconnected(),
  attached = false;                    // so the sketch resets its own state
  candidatesReported = false;
  usb.begin();                         // (or begin(cfg) with the same config as before)
}
```

Notes on this approach:

- The restart cycle costs roughly 1-2 s plus re-enumeration; it happens once per
  unknown adapter, on first plug.
- Storing `vid/pid → configurationValue` in `Preferences` (NVS) lets later boots
  select the right configuration on pass 1, so the restart disappears.
- Latch `forcedConfiguration` before restarting and never restart twice for the
  same device, otherwise an adapter whose candidate never becomes active (or a
  scan that returns nothing) turns into a restart loop.
- The restart re-enumerates *all* attached devices, not just the adapter. When
  the adapter is behind an external hub with per-port power switching,
  `usb.setHubPortPower(hubAddress, port, false/true)` re-enumerates only that
  port instead.

## Notes

- Configuration selection requires Arduino-ESP32 3.3.11 or later.
- The selector only receives the device descriptor, so the configuration number
  cannot be discovered from inside the selector. The candidate report printed on
  connect (via `usb.getNetworkInterfaces()`, which walks every configuration)
  gives the value to hard-code in the selector; after adding the rule, reset the
  board so the device is re-enumerated with that configuration.
- CDC-NCM is preferred over CDC-ECM when a device offers both.
- The interface is opened only from `loop()` context, never from the USB device
  callback (enumeration descriptor access is not allowed on the client task).
- lwIP integration requires `esp_netif` to be available in the build (it is in
  the standard Arduino-ESP32 core). Without it, `networkAttachNetif()` returns
  `false` and the raw frame API can still be used.

## See Also

- [EspUsbDevice UsbNetwork](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/UsbNetwork) - the
  matching USB network device
- `tests/peer/usb_ncm` - the automated two-board CDC-NCM peer test
- `docs/usb-network-spec.ja.md` - the USB network API design notes
