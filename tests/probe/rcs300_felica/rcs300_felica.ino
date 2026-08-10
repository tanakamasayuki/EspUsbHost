#include "EspUsbHost.h"

// Interactive probe for the vendor command set a Sony RC-S300 needs in order to
// run a FeliCa Polling with a chosen System Code. Nothing here is RC-S300
// specific: the sketch is a byte pump, so the command sequence can be searched
// from the host side without reflashing.
//
// Commands read from Serial, one per line:
//   ?          print the CCID interface information
//   s          GetSlotStatus
//   p          IccPowerOn, print the ATR
//   f          IccPowerOff
//   x <hex>    PC_to_RDR_XfrBlock with <hex> as the payload
//   e <hex>    PC_to_RDR_Escape with <hex> as the payload
//   t <ms>     set the timeout used by the next commands
//
// Every command answers with exactly one RSP line so the host side can pair
// requests and responses.

EspUsbHost usb;

static constexpr size_t MAX_PAYLOAD = 256;
static constexpr uint32_t READY_TIMEOUT_MS = 20000;

static bool ready = false;
static uint32_t timeoutMs = 3000;
static char line[2 * MAX_PAYLOAD + 16];
static size_t lineLength = 0;

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

// Parses hex digits, ignoring spaces so "ff 50 00" and "ff500o" style grouping
// both work from the host side.
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

static void runDataExchange(bool escape, const char *argument)
{
    uint8_t tx[MAX_PAYLOAD] = {};
    size_t txLength = 0;
    if (!parseHex(argument, tx, sizeof(tx), &txLength) || txLength == 0)
    {
        Serial.println("RSP parse error");
        return;
    }

    uint8_t rx[MAX_PAYLOAD] = {};
    size_t rxLength = 0;
    const bool ok = escape
                        ? usb.ccidEscape(tx, txLength, rx, sizeof(rx), &rxLength, 0, ESP_USB_HOST_ANY_ADDRESS, timeoutMs)
                        : usb.ccidTransfer(tx, txLength, rx, sizeof(rx), &rxLength, 0, ESP_USB_HOST_ANY_ADDRESS, timeoutMs);
    if (!ok)
    {
        Serial.printf("RSP fail error=0x%02x\n", usb.ccidLastError());
        return;
    }
    Serial.printf("RSP ok len=%u data=", (unsigned)rxLength);
    printHex(rx, rxLength);
    Serial.println();
}

static void runStatus()
{
    EspUsbHostCcidStatus status;
    if (!usb.ccidGetStatus(status))
    {
        Serial.printf("RSP fail error=0x%02x\n", usb.ccidLastError());
        return;
    }
    Serial.printf("RSP ok icc=%u present=%u active=%u command=%u error=0x%02x\n",
                  (unsigned)status.iccStatus,
                  status.present ? 1 : 0,
                  status.active ? 1 : 0,
                  (unsigned)status.commandStatus,
                  status.error);
}

static void runPowerOn()
{
    uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
    size_t atrLength = 0;
    if (!usb.ccidPowerOn(atr, sizeof(atr), &atrLength))
    {
        Serial.printf("RSP fail error=0x%02x\n", usb.ccidLastError());
        return;
    }
    Serial.printf("RSP ok len=%u data=", (unsigned)atrLength);
    printHex(atr, atrLength);
    Serial.println();
}

static void runInterface()
{
    EspUsbHostCcidInterface info;
    if (!usb.ccidGetInterface(info))
    {
        Serial.println("RSP fail no interface");
        return;
    }
    Serial.printf("RSP ok address=%u iface=%u in=0x%02x out=0x%02x interrupt=0x%02x bcd=%04x slots=%u voltage=0x%02x protocols=0x%08lx features=0x%08lx maxMessage=%lu exchange=%u\n",
                  info.address,
                  info.interfaceNumber,
                  info.inEndpoint,
                  info.outEndpoint,
                  info.interruptEndpoint,
                  info.bcdCCID,
                  info.slotCount,
                  info.voltageSupport,
                  (unsigned long)info.protocols,
                  (unsigned long)info.features,
                  (unsigned long)info.maxMessageLength,
                  (unsigned)info.exchangeLevel);
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
    case 's':
        runStatus();
        break;
    case 'p':
        runPowerOn();
        break;
    case 'f':
        Serial.printf("RSP %s\n", usb.ccidPowerOff() ? "ok" : "fail");
        break;
    case 'x':
        runDataExchange(false, argument);
        break;
    case 'e':
        runDataExchange(true, argument);
        break;
    case 't':
    {
        const long value = atol(argument);
        if (value > 0)
        {
            timeoutMs = static_cast<uint32_t>(value);
        }
        Serial.printf("RSP ok timeout=%lu\n", (unsigned long)timeoutMs);
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

    usb.begin();
    Serial.println("TEST_BEGIN rcs300_felica_probe");
}

void loop()
{
    static uint32_t startedAt = millis();

    if (!ready)
    {
        if (usb.ccidOpen())
        {
            ready = true;
            Serial.println("PROBE_READY");
        }
        else if (millis() - startedAt > READY_TIMEOUT_MS)
        {
            startedAt = millis();
            Serial.println("PROBE_NO_READER");
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
