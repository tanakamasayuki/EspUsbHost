#include "EspUsbHost.h"

static constexpr uint8_t ADB_CLASS = 0xff;
static constexpr uint8_t ADB_SUBCLASS = 0x42;
static constexpr uint8_t ADB_PROTOCOL = 0x01;

static constexpr uint32_t A_CNXN = 0x4e584e43;
static constexpr uint32_t A_AUTH = 0x48545541;
static constexpr uint32_t A_VERSION = 0x01000000;
static constexpr uint32_t A_MAXDATA = 4096;
static constexpr uint32_t TEST_TIMEOUT_MS = 30000;
static constexpr uint32_t STABILITY_MS = 2000;

EspUsbHost usb;

static volatile bool connectPending = false;
static uint8_t deviceAddress = 0;
static bool adbOpen = false;
static bool responseReceived = false;
static bool finished = false;
static uint32_t responseAtMs = 0;
static uint8_t rxBuffer[512];
static size_t rxLength = 0;

static void putU32(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    p[2] = static_cast<uint8_t>(value >> 16);
    p[3] = static_cast<uint8_t>(value >> 24);
}

static uint32_t getU32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint32_t adbChecksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i++)
    {
        sum += data[i];
    }
    return sum;
}

static bool adbSend(uint32_t command, uint32_t arg0, uint32_t arg1,
                    const uint8_t *data, uint32_t length)
{
    uint8_t header[24];
    putU32(header + 0, command);
    putU32(header + 4, arg0);
    putU32(header + 8, arg1);
    putU32(header + 12, length);
    putU32(header + 16, adbChecksum(data, length));
    putU32(header + 20, command ^ 0xffffffff);

    if (!usb.vendorWrite(header, sizeof(header), deviceAddress))
    {
        return false;
    }
    return length == 0 || usb.vendorWrite(data, length, deviceAddress);
}

static bool openAdbInterface(uint8_t address)
{
    EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
    const size_t count = usb.getInterfaces(address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
    for (size_t i = 0; i < count; i++)
    {
        const EspUsbHostInterfaceInfo &itf = interfaces[i];
        if (itf.interfaceClass == ADB_CLASS &&
            itf.interfaceSubClass == ADB_SUBCLASS &&
            itf.interfaceProtocol == ADB_PROTOCOL)
        {
            Serial.printf("ADB interface found: address=%u number=%u\n", address, itf.number);
            return usb.vendorOpen(address, itf.number);
        }
    }
    return false;
}

static void fail(const char *reason)
{
    if (!finished)
    {
        finished = true;
        Serial.printf("[FAIL] %s\n", reason);
    }
}

static void processReceived()
{
    while (rxLength >= 24 && !finished)
    {
        const uint32_t command = getU32(rxBuffer + 0);
        const uint32_t dataLength = getU32(rxBuffer + 12);
        const uint32_t checksum = getU32(rxBuffer + 16);
        const uint32_t magic = getU32(rxBuffer + 20);

        if (magic != (command ^ 0xffffffff))
        {
            fail("ADB header magic mismatch");
            return;
        }
        if (dataLength > sizeof(rxBuffer) - 24)
        {
            fail("ADB payload exceeds receive buffer");
            return;
        }
        if (rxLength < 24 + dataLength)
        {
            return;
        }
        if (adbChecksum(rxBuffer + 24, dataLength) != checksum)
        {
            fail("ADB payload checksum mismatch");
            return;
        }

        Serial.printf("ADB response: command=%s length=%u\n",
                      command == A_CNXN ? "CNXN" : command == A_AUTH ? "AUTH" : "other",
                      static_cast<unsigned>(dataLength));
        if (command != A_CNXN && command != A_AUTH)
        {
            fail("first ADB response was neither CNXN nor AUTH");
            return;
        }

        responseReceived = true;
        responseAtMs = millis();
        const size_t consumed = 24 + dataLength;
        memmove(rxBuffer, rxBuffer + consumed, rxLength - consumed);
        rxLength -= consumed;
    }
}

void setup()
{
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address, device.vid, device.pid, device.product);
        if (!adbOpen && openAdbInterface(device.address))
        {
            deviceAddress = device.address;
            adbOpen = true;
            responseReceived = false;
            rxLength = 0;
            // Do not call the synchronous vendorWrite() from this USB client
            // callback. loop() sends A_CNXN from the Arduino task instead.
            connectPending = true;
        } });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        Serial.printf("disconnected address=%u\n", device.address);
        if (device.address == deviceAddress)
        {
            adbOpen = false;
            connectPending = false;
            responseReceived = false;
            deviceAddress = 0;
        } });

    if (!usb.begin())
    {
        fail("usb.begin() failed");
        return;
    }
    Serial.println("adb_connect test start");
    Serial.println("Connect and unlock an Android device with USB debugging enabled.");
}

void loop()
{
    static const uint32_t startedAtMs = millis();

    if (!finished && adbOpen && connectPending)
    {
        connectPending = false;
        static const char banner[] = "host::\0";
        if (!adbSend(A_CNXN, A_VERSION, A_MAXDATA,
                     reinterpret_cast<const uint8_t *>(banner), sizeof(banner)))
        {
            fail("A_CNXN bulk OUT failed");
        }
        else
        {
            Serial.println("A_CNXN sent");
        }
    }

    if (!finished && adbOpen && !responseReceived && rxLength < sizeof(rxBuffer))
    {
        const size_t received = usb.vendorRead(rxBuffer + rxLength,
                                               sizeof(rxBuffer) - rxLength,
                                               deviceAddress);
        if (received > 0)
        {
            rxLength += received;
            processReceived();
        }
    }

    if (!finished && adbOpen && responseReceived && millis() - responseAtMs >= STABILITY_MS)
    {
        finished = true;
        Serial.println("[PASS]");
    }

    if (!finished && millis() - startedAtMs > TEST_TIMEOUT_MS)
    {
        fail("ADB interface or response timeout");
    }

    delay(10);
}
