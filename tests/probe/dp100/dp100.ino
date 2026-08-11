#include "EspUsbHost.h"

// Interactive probe for the ALIENTEK DP100 (ATK-MDP100, 2e3c:af01) HID protocol.
// The device exposes one HID interface with a 64-byte interrupt IN and a 64-byte
// interrupt OUT, so the sketch is a byte pump: frames are composed on the host
// side and the raw answer is printed back, which lets the frame layout and the
// CRC variant be searched without reflashing.
//
// Nothing here is DP100 specific except the default direction byte.
//
// SAFETY: this probe sends whatever it is told to. Every command below is issued
// from the pytest side, and the read-only opcodes are the only ones that run by
// default there. Keep the output terminals unloaded.
//
// Commands read from Serial, one per line:
//   ?            print the HID interface information
//   o <op> [hex] compose a frame from opcode + data (direction, Len and CRC added)
//   x <hex>      send raw bytes as-is, zero padded to the report size
//   v <n>        CRC variant: 0 = MODBUS LE, 1 = MODBUS BE, 2 = skip the
//                direction byte, 3 = no CRC (zeros)
//   d <hex>      set the host->device direction byte (default fb)
//   t <ms>       response timeout
//
// Every command answers with exactly one RSP line so the host side can pair
// requests and responses.

EspUsbHost usb;

static constexpr size_t REPORT_SIZE = 64;
static constexpr size_t MAX_DATA = REPORT_SIZE;
static constexpr uint32_t READY_TIMEOUT_MS = 20000;

static bool ready = false;
static uint32_t timeoutMs = 1000;
static uint8_t crcVariant = 0;
static uint8_t directionByte = 0xfb;
static char line[2 * REPORT_SIZE + 32];
static size_t lineLength = 0;

// Last report received on the HID interrupt IN endpoint. onHIDInput() runs on the
// USB task, so it only latches; everything else happens from loop().
static volatile bool responseReady = false;
static uint8_t response[REPORT_SIZE];
static volatile size_t responseLength = 0;
static volatile uint32_t responseCount = 0;
static uint8_t probeAddress = 0;

static void printHex(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        Serial.printf("%02x", data[i]);
    }
}

static int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

// Parses hex digits, ignoring separators so "fb 10 00" and "fb1000" both work.
static bool parseHex(const char *text, uint8_t *out, size_t capacity, size_t *length)
{
    size_t count = 0;
    int high = -1;
    for (const char *p = text; *p != '\0'; p++)
    {
        if (*p == ' ' || *p == '\t' || *p == ':' || *p == '-')
        {
            continue;
        }
        const int digit = hexDigit(*p);
        if (digit < 0)
        {
            return false;
        }
        if (high < 0)
        {
            high = digit;
            continue;
        }
        if (count >= capacity)
        {
            return false;
        }
        out[count++] = static_cast<uint8_t>((high << 4) | digit);
        high = -1;
    }
    if (high >= 0)
    {
        return false;
    }
    *length = count;
    return true;
}

// CRC-16/MODBUS: reflected polynomial 0xa001, init 0xffff, no final xor. Written
// bit by bit rather than from a table so the probe has no lookup data to be wrong
// about.
static uint16_t crc16Modbus(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xa001) : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

static void clearResponse()
{
    responseReady = false;
    responseLength = 0;
}

// Sends one report and waits for the next one to arrive. The send is asynchronous,
// so a false here means the transfer was not even queued.
static void sendAndReport(const uint8_t *frame, size_t frameLength, const char *label)
{
    uint8_t report[REPORT_SIZE] = {};
    if (frameLength > sizeof(report))
    {
        Serial.println("RSP too long");
        return;
    }
    memcpy(report, frame, frameLength);

    clearResponse();
    if (!usb.sendHIDVendorOutput(report, sizeof(report), probeAddress))
    {
        Serial.println("RSP send fail");
        return;
    }

    const uint32_t startedAt = millis();
    while (!responseReady && millis() - startedAt < timeoutMs)
    {
        delay(1);
    }
    if (!responseReady)
    {
        Serial.printf("RSP timeout sent=%s ", label);
        printHex(report, frameLength);
        Serial.println();
        return;
    }

    Serial.printf("RSP ok len=%u data=", static_cast<unsigned>(responseLength));
    printHex(response, responseLength);
    Serial.println();
}

// Composes [direction][opcode][reserved][len][data...][crc] per the selected CRC
// variant. Which variant the device answers is what identifies the real one.
static void runOpcode(char *argument)
{
    char *space = strchr(argument, ' ');
    if (space)
    {
        *space = '\0';
    }
    const long opcode = strtol(argument, nullptr, 16);
    if (opcode < 0 || opcode > 0xff)
    {
        Serial.println("RSP parse error");
        return;
    }

    uint8_t data[MAX_DATA] = {};
    size_t dataLength = 0;
    if (space && !parseHex(space + 1, data, sizeof(data), &dataLength))
    {
        Serial.println("RSP parse error");
        return;
    }

    uint8_t frame[REPORT_SIZE] = {};
    if (4 + dataLength + 2 > sizeof(frame))
    {
        Serial.println("RSP too long");
        return;
    }
    frame[0] = directionByte;
    frame[1] = static_cast<uint8_t>(opcode);
    frame[2] = 0x00;
    frame[3] = static_cast<uint8_t>(dataLength);
    memcpy(frame + 4, data, dataLength);

    size_t frameLength = 4 + dataLength;
    switch (crcVariant)
    {
    case 0:
    {
        const uint16_t crc = crc16Modbus(frame, frameLength);
        frame[frameLength++] = static_cast<uint8_t>(crc & 0xff);
        frame[frameLength++] = static_cast<uint8_t>(crc >> 8);
        break;
    }
    case 1:
    {
        const uint16_t crc = crc16Modbus(frame, frameLength);
        frame[frameLength++] = static_cast<uint8_t>(crc >> 8);
        frame[frameLength++] = static_cast<uint8_t>(crc & 0xff);
        break;
    }
    case 2:
    {
        // Same CRC but computed from the opcode on, in case the direction byte is
        // outside the checked range.
        const uint16_t crc = crc16Modbus(frame + 1, frameLength - 1);
        frame[frameLength++] = static_cast<uint8_t>(crc & 0xff);
        frame[frameLength++] = static_cast<uint8_t>(crc >> 8);
        break;
    }
    default:
        frame[frameLength++] = 0x00;
        frame[frameLength++] = 0x00;
        break;
    }

    char label[16];
    snprintf(label, sizeof(label), "op=%02x/v%u", static_cast<unsigned>(opcode), crcVariant);
    sendAndReport(frame, frameLength, label);
}

static void runRaw(char *argument)
{
    uint8_t frame[REPORT_SIZE] = {};
    size_t frameLength = 0;
    if (!parseHex(argument, frame, sizeof(frame), &frameLength) || frameLength == 0)
    {
        Serial.println("RSP parse error");
        return;
    }
    sendAndReport(frame, frameLength, "raw");
}

static void runInterface()
{
    EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
    const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
    for (size_t i = 0; i < count; i++)
    {
        EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
        const size_t interfaceCount =
            usb.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
        for (size_t n = 0; n < interfaceCount; n++)
        {
            if (interfaces[n].interfaceClass != 0x03)
            {
                continue;
            }
            Serial.printf("RSP ok address=%u vid=%04x pid=%04x iface=%u subclass=0x%02x protocol=0x%02x product=\"%s\" serial=\"%s\" received=%lu\n",
                          devices[i].address,
                          devices[i].vid,
                          devices[i].pid,
                          interfaces[n].number,
                          interfaces[n].interfaceSubClass,
                          interfaces[n].interfaceProtocol,
                          devices[i].product,
                          devices[i].serial,
                          static_cast<unsigned long>(responseCount));
            return;
        }
    }
    Serial.println("RSP fail no HID interface");
}

static void handleLine(char *text)
{
    while (*text == ' ')
    {
        text++;
    }
    if (*text == '\0')
    {
        return;
    }

    const char command = *text;
    char *argument = text + 1;
    while (*argument == ' ')
    {
        argument++;
    }

    switch (command)
    {
    case '?':
        runInterface();
        break;
    case 'o':
        runOpcode(argument);
        break;
    case 'x':
        runRaw(argument);
        break;
    case 'v':
    {
        const long value = atol(argument);
        if (value >= 0 && value <= 3)
        {
            crcVariant = static_cast<uint8_t>(value);
        }
        Serial.printf("RSP ok crc_variant=%u\n", crcVariant);
        break;
    }
    case 'd':
    {
        uint8_t value[1] = {};
        size_t length = 0;
        if (parseHex(argument, value, sizeof(value), &length) && length == 1)
        {
            directionByte = value[0];
        }
        Serial.printf("RSP ok direction=%02x\n", directionByte);
        break;
    }
    case 't':
    {
        const long value = atol(argument);
        if (value > 0)
        {
            timeoutMs = static_cast<uint32_t>(value);
        }
        Serial.printf("RSP ok timeout=%lu\n", static_cast<unsigned long>(timeoutMs));
        break;
    }
    default:
        Serial.println("RSP unknown command");
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          { Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                                          device.address, device.vid, device.pid, device.product); });

    // Every HID IN report, before the report-ID dispatch that would never match a
    // frame whose first byte is a protocol header.
    usb.onHIDInput([](const EspUsbHostHIDInput &input)
                   {
        size_t length = input.length;
        if (length > REPORT_SIZE)
        {
            length = REPORT_SIZE;
        }
        memcpy(response, input.data, length);
        responseLength = length;
        responseCount++;
        responseReady = true; });

    usb.begin();
    Serial.println("TEST_BEGIN dp100_probe");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!ready)
    {
        EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
        const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
        for (size_t i = 0; i < count; i++)
        {
            EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
            const size_t interfaceCount =
                usb.getInterfaces(devices[i].address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
            for (size_t n = 0; n < interfaceCount; n++)
            {
                if (interfaces[n].interfaceClass == 0x03 && interfaces[n].claimed)
                {
                    probeAddress = devices[i].address;
                    ready = true;
                    Serial.println("PROBE_READY");
                    break;
                }
            }
            if (ready)
            {
                break;
            }
        }
        if (!ready && millis() - startedAt > READY_TIMEOUT_MS)
        {
            startedAt = millis();
            Serial.println("PROBE_NO_DEVICE");
        }
        delay(200);
        return;
    }

    while (Serial.available() > 0)
    {
        const int c = Serial.read();
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            line[lineLength] = '\0';
            lineLength = 0;
            handleLine(line);
            continue;
        }
        if (lineLength + 1 < sizeof(line))
        {
            line[lineLength++] = static_cast<char>(c);
        }
    }

    delay(5);
}
