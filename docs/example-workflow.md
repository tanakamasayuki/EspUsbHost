# Working through the examples

[日本語](example-workflow.ja.md)

A practical route from wiring through observation and adaptation for EspUsbHost. The ESP32 controls attached USB peripherals such as keyboards, serial adapters and storage. A PC is normally also a host, so it is not the peripheral to plug into the host data port for this experiment.

## Terms used in these guides

| Term | Meaning here |
| --- | --- |
| Enumeration | The host reads device information and chooses an address/configuration |
| Descriptor | Information describing a device's functions and data formats |
| Interface | A function-level grouping within one USB device; composites may have several |
| Endpoint | A data entry/exit with a number, direction and transfer type |
| Report / packet / frame | Class/protocol-defined units, not interchangeable byte counts |
| Callback | A function registered beforehand and invoked by the library for an event/request |
| Buffer / queue | Finite storage for data waiting to be processed |
| Timeout | A limit on waiting for a peer so an absent response can be handled |

## Choose a starting point

1. Use [BringUpCheck](../examples/Info/EspUsbHostBringUpCheck/README.md) to separate host startup from peripheral enumeration.
2. Read VID/PID, interfaces and endpoints with [DeviceInfo](../examples/Info/EspUsbHostDeviceInfo/README.md).
3. Run one class example for a device you own, such as [Keyboard](../examples/HID/EspUsbHostKeyboard/README.md).
4. For unfamiliar HID, compare [RawDump](../examples/HID/EspUsbHostHIDRawDump/README.md) with [ReportDescriptor](../examples/Info/EspUsbHostHIDReportDescriptor/README.md).
5. Add hubs, multiple devices, audio and custom protocols after the single-device case works.

The [complete example index](../examples/README.md) links to each guide's components, operation, APIs and adaptation notes. Keep an example's whole folder when it has adjacent protocol helpers.

## Install and build

1. Follow the [library README](../README.md) to install Arduino-ESP32 and the library. Use its support/compatibility references for target and core requirements, then inspect the example's profile.
2. Open File → Examples → EspUsbHost in Arduino IDE and select the exact board. From the repository root, a CLI build can use:

```sh
arduino-cli compile --profile esp32s3 examples/HID/EspUsbHostKeyboard
```

3. `sketch.yaml` declares that profile's platform and libraries. Check local-library references; examples with external libraries or generated assets require the adjacent README's preparation. Copying only the .ino can omit helpers.
4. Upload through a known working route and open diagnostics using the sketch's Serial.begin() settings. A successful compile does not verify physical wiring or interoperability.

## Identify connectors and power first

A connector marked USB may lead to OTG data pads, USB Serial/JTAG or a USB-UART bridge. Check the schematic for the controller, D−/D+, PHY, VBUS and USB-C CC/role circuitry. Use a data-capable cable and compatible signal levels.

A host supplies the required VBUS to a peripheral; a device connects to a host. Follow the board's power design rather than tying independent supply outputs together. Selecting a P4 controller or PHY does not automatically change connector wiring or power circuitry.

Serial is the sketch's diagnostic path. Depending on board/settings it may use USB or UART; verify where it can actually be read. Treat it separately from CDC or HID under test. Before moving Serial/JTAG pads, arrange an independent log/upload recovery route such as UART.

## Read the sketch in order

1. Read the overview and pin/format/command constants, including their units.
2. Identify the global USB owner and the functions/helpers registered on it.
3. Follow callback registration, medium/format preparation and begin() in setup().
4. Follow buffered input, state changes, periodic work and pending requests in loop().
5. Distinguish success booleans, actual byte counts and state queries. Failure does not automatically implement a retry policy.

## Interpret state precisely

| Observation | Meaning |
| --- | --- |
| `usb.begin()` succeeds | Internal host startup; no promise that a peripheral exists or is supported |
| Connection notification and device information | An enumerated device; class readiness is a separate check |
| Class ready/open succeeds | The interface is usable; protocol response is another stage |
| Transfer/write succeeds | The acceptance/completion defined by that API, not an application acknowledgment |
| Disconnect | Invalidate old address/interface/session assumptions and reacquire them on reconnect |

USB IN/OUT is always named from the device's viewpoint: IN is device→host, OUT is host→device. A request or response uses whichever direction its protocol defines; the naming does not reverse when application roles change.

## Separate callbacks from loop()

USB callbacks run on a USB processing task separate from loop(). Registering one does not require calling it manually from loop(). Long logging, waiting, decoding or file I/O in a callback delays subsequent USB work.

Do not add waiting vendorControlTransfer(), vendorWrite(), MSC, CCID or end() calls to USB client callbacks. Record requested work and execute it from loop() or an application task. Distinguish the callback-aware configuration APIs used by an example from waiting transfer APIs.

Callback data/report/frame pointers can be recycled on return. Copy what must outlive the callback into owned storage and define its handoff/completion point. An asynchronous/direct transfer buffer must also remain unchanged until completion. Do not confuse a copying write contract with a borrowed direct-buffer contract.

volatile alone does not provide mutual exclusion, ordering or consistency across several fields shared between tasks. Use a synchronized handoff such as a FreeRTOS queue, submit without waiting from callbacks, and decide whether full queues cause dropping, counting or retry. Do not wait in USB/Serial operations while holding a critical section.

## Finite buffers and failures

- Interpret only the actual read length. A requested size need not arrive in full, and one callback need not represent one command.
- A short/failed write needs an explicit policy: retain the unsent suffix, or count a deliberate discard. Logging an error is not recovery.
- Pair HID presses/releases and MIDI Note On/Off; respect frame ownership for video and distinguish samples, frames and bytes for audio.
- Distinguish storage blocks from filesystems. Give a host-mounted medium only one independent writer.
- Keep heavy print output out of callbacks and throughput measurements; terminal speed can limit the experiment.

## Troubleshooting by stage

| Symptom | Next check |
| --- | --- |
| No diagnostics | Board settings, Serial route, terminal parameters and port-routing changes |
| begin() fails | Immediate error name, controller choice, stack conflicts and required resources |
| Startup succeeds but no enumeration | Data connector/cable, VBUS/roles, selected controller and PHY |
| Enumerates but class does not work | Interfaces/endpoints, report descriptor and peer format support |
| Works initially or loses data under load | Actual lengths, accepted counts, full queues, callback duration and buffer lifetime |
| Fails after reconnect | Stale address/session, DTR, held keys and pending-transfer state |

Try peer absent, connected, normal operation, disconnect and reconnect in order, recording each stage. Before settings, printing or Flash updates, read the individual guide to understand the physical operation.

For principles see the [USB development guide](usb-host-guide.md), for execution/transfer contracts the [advanced guide](usb-host-advanced.md), and for public APIs the [library README](../README.md). Keep version-specific support and measurements in their dedicated references.
