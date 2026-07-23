# EspUsbHost UsbNetwork

> 日本語版: [README.ja.md](README.ja.md)

> ⚠️ **Experimental.** Arduino-ESP32 3.3.11 and later can select the active USB
> configuration during enumeration. This example selects CDC-NCM configuration
> 2 for the AX88179A (`0b95:1790`). For another adapter, inspect it with
> `tests/manual/usb_network_descriptor` and add a selector rule.

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

## Notes

- Configuration selection requires Arduino-ESP32 3.3.11 or later.
- The selector only receives the device descriptor, so the configuration number
  must be determined beforehand.
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
