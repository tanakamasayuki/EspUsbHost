# EspUsbHost UsbNetwork

> 日本語版: [README.ja.md](README.ja.md)

Turns the board into a USB *host* for a USB Ethernet adapter (CDC-NCM / CDC-ECM)
and brings it up as an lwIP network interface. Plug in a USB NIC — or a second
board running the sibling
[EspUsbDevice `UsbNetwork`](../../../EspUsbDevice/examples/UsbNetwork/) sketch —
and standard Arduino networking (`NetworkClient`, `HTTPClient`) runs over USB,
with no Wi-Fi.

This is the counterpart to the EspUsbDevice `UsbNetwork` example: that sketch is
the network *device* (with its own DHCP server at `192.168.7.1`); this sketch is
the network *host* that receives a `192.168.7.x` lease and can reach it.

## Hardware

- ESP32-S3 (or another Arduino-ESP32 board with USB host support) as the host
- A USB network adapter, or a second board running EspUsbDevice `UsbNetwork`
- A separate Serial monitor connection for logs

## What It Does

- Enumerates the USB device and, if it exposes a CDC-NCM/ECM interface, attaches
  it as a DHCP-client lwIP netif with `networkAttachNetif()`
- Prints the acquired IP address
- Does an `HTTPClient` GET to `http://192.168.7.1/` to show that TCP/IP runs over
  the USB link

## Key APIs

- `usb.networkAttachNetif(cfg, address)` opens the network interface (if needed)
  and registers it as an `esp_netif` netif. `EspUsbHostNetworkConfig` defaults to
  a DHCP client; set `dhcpClient=false` and fill `ip`/`gateway`/`subnet`(`/dns1`)
  for a static address.
- `usb.networkLocalIP(address)` reports the interface address once leased.
- `usb.networkDetachNetif(address)` tears the netif down (also done automatically
  on USB disconnect).
- For raw Ethernet frames instead of an IP stack, use `usb.onNetworkFrame()` /
  `usb.networkWriteFrame()` / `usb.networkReadFrame()` and do not attach a netif.

## Notes

- CDC-NCM is preferred over CDC-ECM when a device offers both.
- The interface is opened only from `loop()` context, never from the USB device
  callback (enumeration descriptor access is not allowed on the client task).
- lwIP integration requires `esp_netif` to be available in the build (it is in
  the standard Arduino-ESP32 core). Without it, `networkAttachNetif()` returns
  `false` and the raw frame API can still be used.

## See Also

- [EspUsbDevice UsbNetwork](../../../EspUsbDevice/examples/UsbNetwork/) - the
  matching USB network device
- `tests/peer/usb_ncm` - the automated two-board CDC-NCM peer test
- `docs/usb-network-spec.ja.md` - the USB network API design notes
