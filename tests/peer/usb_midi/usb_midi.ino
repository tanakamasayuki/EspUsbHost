#include "EspUsbHost.h"

EspUsbHost usb;

static void installMidiCallback();

static volatile bool listenerTestActive = false;
static volatile bool listenerMutated = false;
static EspUsbHostListenerId midiListenerIds[EspUsbHost::MaxListenersPerEvent] = {};
static EspUsbHostListenerId replacementMidiListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;

static volatile bool lifecycleTestActive = false;
static EspUsbHostListenerId connectListenerIds[EspUsbHost::MaxLifecycleListeners] = {};
static EspUsbHostListenerId disconnectListenerIds[EspUsbHost::MaxLifecycleListeners] = {};

static void clearMidiListenerTest()
{
    for (size_t i = 0; i < EspUsbHost::MaxListenersPerEvent; i++)
    {
        if (midiListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID)
        {
            usb.removeListener(midiListenerIds[i]);
            midiListenerIds[i] = ESP_USB_HOST_INVALID_LISTENER_ID;
        }
    }
    if (replacementMidiListenerId != ESP_USB_HOST_INVALID_LISTENER_ID)
    {
        usb.removeListener(replacementMidiListenerId);
        replacementMidiListenerId = ESP_USB_HOST_INVALID_LISTENER_ID;
    }
    listenerTestActive = false;
    listenerMutated = false;
    installMidiCallback();
}

static void clearLifecycleListenerTest()
{
    for (size_t i = 0; i < EspUsbHost::MaxLifecycleListeners; i++)
    {
        if (connectListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID)
        {
            usb.removeListener(connectListenerIds[i]);
            connectListenerIds[i] = ESP_USB_HOST_INVALID_LISTENER_ID;
        }
        if (disconnectListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID)
        {
            usb.removeListener(disconnectListenerIds[i]);
            disconnectListenerIds[i] = ESP_USB_HOST_INVALID_LISTENER_ID;
        }
    }
    lifecycleTestActive = false;
}

static void installMidiCallback()
{
    usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                      { Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                                      message.cable,
                                      message.codeIndex,
                                      message.status,
                                      message.data1,
                                      message.data2); });
}

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    installMidiCallback();

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("HOST_CONNECTED vid=%04x pid=%04x supported=%u\n",
                                          device.vid,
                                          device.pid,
                                          device.supported ? 1 : 0); });
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 (void)device;
                                 Serial.println("HOST_DISCONNECTED");
                             });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'n')
        {
            Serial.printf("MIDI_TX_NOTE_ON %u\n", usb.midiSendNoteOn(0, 60, 100) ? 1 : 0);
        }
        else if (command == 'f')
        {
            Serial.printf("MIDI_TX_NOTE_OFF %u\n", usb.midiSendNoteOff(0, 60, 0) ? 1 : 0);
        }
        else if (command == 'c')
        {
            Serial.printf("MIDI_TX_CC %u\n", usb.midiSendControlChange(0, 74, 64) ? 1 : 0);
        }
        else if (command == 'p')
        {
            Serial.printf("MIDI_TX_PROGRAM %u\n", usb.midiSendProgramChange(0, 10) ? 1 : 0);
        }
        else if (command == 'b')
        {
            Serial.printf("MIDI_TX_BEND %u\n", usb.midiSendPitchBend(0, 8192 + 1024) ? 1 : 0);
        }
        else if (command == 'a')
        {
            Serial.printf("MIDI_TX_PRESSURE %u\n", usb.midiSendChannelPressure(0, 77) ? 1 : 0);
        }
        else if (command == 'y')
        {
            Serial.printf("MIDI_TX_POLY_PRESSURE %u\n", usb.midiSendPolyPressure(0, 60, 80) ? 1 : 0);
        }
        else if (command == 's')
        {
            const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
            Serial.printf("MIDI_TX_SYSEX %u\n", usb.midiSendSysEx(sysex, sizeof(sysex)) ? 1 : 0);
        }
        else if (command == 'i')
        {
            EspUsbHostMidiPortInfo info;
            const bool ok = usb.getMidiPortInfo(info);
            // The interface number is last so a test can match the cable counts
            // without pinning the peer's interface layout.
            Serial.printf("MIDI_PORT_INFO ok=%u in=%u out=%u iface=%u\n",
                          ok ? 1 : 0,
                          info.inCableCount,
                          info.outCableCount,
                          info.interfaceNumber);
        }
        else if (command == 'l')
        {
            clearMidiListenerTest();
            const bool emptyRejected =
                usb.addMidiMessageListener(EspUsbHost::MidiMessageCallback()) == ESP_USB_HOST_INVALID_LISTENER_ID;
            midiListenerIds[0] = usb.addMidiMessageListener([](const EspUsbHostMidiMessage &message)
                                                           {
                if (!listenerTestActive)
                {
                    return;
                }
                Serial.printf("MIDI_LISTENER 1 status=%02x\n", message.status);
                if (!listenerMutated)
                {
                    // Removing and adding from inside a callback must not affect
                    // this event: the delivery set was snapshotted before it ran.
                    listenerMutated = true;
                    usb.removeListener(midiListenerIds[0]);
                    midiListenerIds[0] = ESP_USB_HOST_INVALID_LISTENER_ID;
                    replacementMidiListenerId = usb.addMidiMessageListener([](const EspUsbHostMidiMessage &next)
                                                                          {
                        if (listenerTestActive)
                        {
                            Serial.printf("MIDI_LISTENER 5 status=%02x\n", next.status);
                        } });
                } });
            midiListenerIds[1] = usb.addMidiMessageListener([](const EspUsbHostMidiMessage &message)
                                                           {
                if (listenerTestActive)
                {
                    Serial.printf("MIDI_LISTENER 2 status=%02x\n", message.status);
                } });
            midiListenerIds[2] = usb.addMidiMessageListener([count = 0](const EspUsbHostMidiMessage &) mutable
                                                           {
                if (listenerTestActive)
                {
                    count++;
                    Serial.printf("MIDI_LISTENER_STATE count=%u\n", static_cast<unsigned>(count));
                } });
            midiListenerIds[3] = usb.addMidiMessageListener([](const EspUsbHostMidiMessage &) {});
            bool allIdsValid = true;
            for (size_t i = 0; i < EspUsbHost::MaxListenersPerEvent; i++)
            {
                allIdsValid = allIdsValid && midiListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID;
            }
            const bool capacityRejected =
                usb.addMidiMessageListener([](const EspUsbHostMidiMessage &) {}) == ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool invalidRemoveRejected = !usb.removeListener(ESP_USB_HOST_INVALID_LISTENER_ID) &&
                                               !usb.removeListener(0xffffffffu);
            listenerTestActive = true;
            Serial.printf("MIDI_LISTENER_SETUP empty=%u ids=%u capacity=%u invalid_remove=%u max=%u\n",
                          emptyRejected ? 1 : 0,
                          allIdsValid ? 1 : 0,
                          capacityRejected ? 1 : 0,
                          invalidRemoveRejected ? 1 : 0,
                          static_cast<unsigned>(EspUsbHost::MaxListenersPerEvent));
        }
        else if (command == 'u')
        {
            const EspUsbHostListenerId removedId = midiListenerIds[1];
            const bool removed = usb.removeListener(removedId);
            midiListenerIds[1] = ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool secondRemoveRejected = !usb.removeListener(removedId);
            Serial.printf("MIDI_LISTENER_REMOVE removed=%u second=%u\n",
                          removed ? 1 : 0,
                          secondRemoveRejected ? 1 : 0);
        }
        else if (command == 'd')
        {
            // Drop the single callback so the next message proves listeners are
            // delivered on their own, not as a side effect of the callback path.
            usb.onMidiMessage(EspUsbHost::MidiMessageCallback());
            Serial.println("MIDI_CALLBACK_DROP 1");
        }
        else if (command == 'e')
        {
            installMidiCallback();
            Serial.println("MIDI_CALLBACK_RESTORE 1");
        }
        else if (command == 'x')
        {
            clearMidiListenerTest();
            Serial.println("MIDI_LISTENER_CLEAR 1");
        }
        else if (command == 'k')
        {
            clearLifecycleListenerTest();
            const bool emptyRejected =
                usb.addDeviceConnectedListener(EspUsbHost::DeviceCallback()) == ESP_USB_HOST_INVALID_LISTENER_ID &&
                usb.addDeviceDisconnectedListener(EspUsbHost::DeviceCallback()) == ESP_USB_HOST_INVALID_LISTENER_ID;
            connectListenerIds[0] = usb.addDeviceConnectedListener([](const EspUsbHostDeviceInfo &device)
                                                                  {
                if (lifecycleTestActive)
                {
                    Serial.printf("CONNECT_LISTENER 1 vid=%04x supported=%u\n",
                                  device.vid,
                                  device.supported ? 1 : 0);
                } });
            connectListenerIds[1] = usb.addDeviceConnectedListener([](const EspUsbHostDeviceInfo &)
                                                                  {
                if (lifecycleTestActive)
                {
                    Serial.println("CONNECT_LISTENER 2");
                } });
            disconnectListenerIds[0] = usb.addDeviceDisconnectedListener([](const EspUsbHostDeviceInfo &device)
                                                                        {
                if (lifecycleTestActive)
                {
                    // The event must still carry usable device information while
                    // the listener runs, after the endpoints are torn down.
                    Serial.printf("DISCONNECT_LISTENER 1 vid=%04x address=%u\n",
                                  device.vid,
                                  device.address);
                } });
            disconnectListenerIds[1] = usb.addDeviceDisconnectedListener([](const EspUsbHostDeviceInfo &)
                                                                        {
                if (lifecycleTestActive)
                {
                    Serial.println("DISCONNECT_LISTENER 2");
                } });
            // Fill the rest of the lifecycle budget, which is deliberately larger
            // than the per-input-event budget, and check that it is exhausted at
            // the documented count rather than earlier or later.
            for (size_t i = 2; i < EspUsbHost::MaxLifecycleListeners; i++)
            {
                connectListenerIds[i] = usb.addDeviceConnectedListener([](const EspUsbHostDeviceInfo &) {});
                disconnectListenerIds[i] = usb.addDeviceDisconnectedListener([](const EspUsbHostDeviceInfo &) {});
            }
            bool allIdsValid = true;
            for (size_t i = 0; i < EspUsbHost::MaxLifecycleListeners; i++)
            {
                allIdsValid = allIdsValid &&
                              connectListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID &&
                              disconnectListenerIds[i] != ESP_USB_HOST_INVALID_LISTENER_ID;
            }
            const bool capacityRejected =
                usb.addDeviceConnectedListener([](const EspUsbHostDeviceInfo &) {}) ==
                    ESP_USB_HOST_INVALID_LISTENER_ID &&
                usb.addDeviceDisconnectedListener([](const EspUsbHostDeviceInfo &) {}) ==
                    ESP_USB_HOST_INVALID_LISTENER_ID;
            lifecycleTestActive = true;
            Serial.printf("LIFECYCLE_LISTENER_SETUP empty=%u ids=%u capacity=%u max=%u\n",
                          emptyRejected ? 1 : 0,
                          allIdsValid ? 1 : 0,
                          capacityRejected ? 1 : 0,
                          static_cast<unsigned>(EspUsbHost::MaxLifecycleListeners));
        }
        else if (command == 'j')
        {
            const EspUsbHostListenerId removedId = connectListenerIds[1];
            const bool removed = usb.removeListener(removedId);
            connectListenerIds[1] = ESP_USB_HOST_INVALID_LISTENER_ID;
            const bool secondRemoveRejected = !usb.removeListener(removedId);
            Serial.printf("CONNECT_LISTENER_REMOVE removed=%u second=%u\n",
                          removed ? 1 : 0,
                          secondRemoveRejected ? 1 : 0);
        }
        else if (command == 'r')
        {
            // end() + begin() re-enumerates the attached peer, which is the
            // deterministic way to produce a connect event on demand.
            usb.end();
            Serial.println("HOST_END 1");
            Serial.printf("HOST_REBEGIN %u\n", usb.begin() ? 1 : 0);
        }
        else if (command == 'z')
        {
            clearLifecycleListenerTest();
            Serial.println("LIFECYCLE_LISTENER_CLEAR 1");
        }
    }
    delay(1);
}
