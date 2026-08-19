# EspUsbHostP4FsPhyRouting

> 日本語版: [README.ja.md](README.ja.md)

Routes the ESP32-P4 Full-speed OTG controller to FSLS PHY0 (GPIO24/GPIO25) before starting `EspUsbHost`.

## When to use it

Use this only on an ESP32-P4 board whose intended USB Host connector is wired to GPIO24/GPIO25 and whose hardware supports Host-mode VBUS and, for USB-C, role/CC handling. M5Stack Tab5 is an example of a board where the USB-C data pair is connected to GPIO24/GPIO25.

The default P4 mapping connects USB Serial/JTAG to GPIO24/GPIO25 and USB OTG FS to GPIO26/GPIO27. This example temporarily swaps them one-for-one in software; it does not duplicate either USB signal:

| Function | Before the call | After `usb_wrap_ll_phy_select(&USB_WRAP, 0)` |
|----------|-----------------|------------------------------------------------|
| USB OTG FS | GPIO26/GPIO27 | GPIO24/GPIO25 |
| USB Serial/JTAG | GPIO24/GPIO25 | GPIO26/GPIO27 |

No eFuse is modified. Resetting the chip restores startup control to the eFuse/default mapping.

## Important

- The routing call must run before `usb.begin()`.
- It must also run before any other OTG FS Host or Device driver is initialized. Stop and uninstall an existing FS driver before changing the route.
- USB Serial/JTAG on GPIO24/GPIO25 disconnects when the mapping changes. Logs after that point require an external USB-to-UART bridge or another console connection.
- If built-in USB Serial/JTAG is enabled, it can re-enumerate from GPIO26/GPIO27 after the swap; the call does not create a second CDC stack.
- The operation changes only the USB D+/D- route. It does not enable VBUS power, over-current protection, or USB-C Host role handling.
- Ensure the GPIO26/GPIO27 connector is not sourcing Host VBUS or connected to hardware that conflicts with the relocated USB Serial/JTAG device role.
- GPIO26/GPIO27 cannot be used as ordinary GPIOs or by another peripheral while USB Serial/JTAG is mapped there. Stop that use before switching. If the USB route is later restored, run `pinMode()` again or restart the peripheral driver to restore its pin mux, direction, and pull configuration.
- Do not burn `USB_PHY_SEL` eFuse just to run this example. eFuse changes are irreversible.
- This example uses an ESP-IDF P4 low-level HAL API and is intentionally kept in application code rather than hidden inside the portable library API.

Connect a USB device to the board connector backed by GPIO24/GPIO25, then run the sketch. The library starts the Full-speed Host peripheral with `ESP_USB_HOST_PORT_FULL_SPEED`.

## Expected Serial output

```
Routing USB OTG FS to GPIO24/GPIO25 in 3 seconds.
USB Serial/JTAG on GPIO24/GPIO25 will disconnect.
```

After these two lines the mapping switches and the USB Serial/JTAG console on GPIO24/GPIO25 disconnects, so nothing further appears there. Any later message, such as `usb.begin failed: ...`, is visible only through an external USB-to-UART bridge or another console connection.
