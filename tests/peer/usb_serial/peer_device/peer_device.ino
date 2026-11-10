#include "USB.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC USBSerial;
#endif

volatile bool lineCodingSeen = false;
volatile uint32_t lineCodingBaud = 0;
volatile uint8_t lineCodingStopBits = 0;
volatile uint8_t lineCodingParity = 0;
volatile uint8_t lineCodingDataBits = 0;

// SET_CONTROL_LINE_STATE as the device sees it. The core drops a request that
// repeats the current state, so every request that changes DTR or RTS lands
// here exactly once; the history lets the test tell one combined request from
// two single-line requests that pass through an intermediate state.
static constexpr size_t LINE_STATE_HISTORY = 16;
portMUX_TYPE lineStateMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t lineStateCount = 0;
bool lineStateDtr = false;
bool lineStateRts = false;
char lineStateHistory[LINE_STATE_HISTORY][3] = {};

void onUsbCdcEvent(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
    (void)arg;
    (void)eventBase;
    if (eventId != ARDUINO_USB_CDC_LINE_CODING_EVENT || !eventData)
    {
        return;
    }
    arduino_usb_cdc_event_data_t *data = static_cast<arduino_usb_cdc_event_data_t *>(eventData);
    lineCodingBaud = data->line_coding.bit_rate;
    lineCodingStopBits = data->line_coding.stop_bits;
    lineCodingParity = data->line_coding.parity;
    lineCodingDataBits = data->line_coding.data_bits;
    lineCodingSeen = true;
}

void onUsbCdcLineState(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData)
{
    (void)arg;
    (void)eventBase;
    if (eventId != ARDUINO_USB_CDC_LINE_STATE_EVENT || !eventData)
    {
        return;
    }
    const arduino_usb_cdc_event_data_t *data = static_cast<const arduino_usb_cdc_event_data_t *>(eventData);
    portENTER_CRITICAL(&lineStateMux);
    lineStateDtr = data->line_state.dtr;
    lineStateRts = data->line_state.rts;
    if (lineStateCount < LINE_STATE_HISTORY)
    {
        lineStateHistory[lineStateCount][0] = lineStateDtr ? '1' : '0';
        lineStateHistory[lineStateCount][1] = lineStateRts ? '1' : '0';
        lineStateHistory[lineStateCount][2] = '\0';
    }
    lineStateCount++;
    portEXIT_CRITICAL(&lineStateMux);
}

static void printLineState()
{
    char history[LINE_STATE_HISTORY * 3 + 1] = {};
    portENTER_CRITICAL(&lineStateMux);
    const uint32_t count = lineStateCount;
    const bool dtr = lineStateDtr;
    const bool rts = lineStateRts;
    size_t used = 0;
    for (uint32_t i = 0; i < count && i < LINE_STATE_HISTORY; i++)
    {
        if (i > 0)
        {
            history[used++] = ',';
        }
        history[used++] = lineStateHistory[i][0];
        history[used++] = lineStateHistory[i][1];
    }
    portEXIT_CRITICAL(&lineStateMux);
    Serial.printf("DEVICE_LINE_STATE count=%lu dtr=%u rts=%u hist=%s\n",
                  static_cast<unsigned long>(count), dtr ? 1 : 0, rts ? 1 : 0,
                  used ? history : "-");
}

void setup()
{
    Serial.begin(115200);
    USBSerial.enableReboot(false);
    USBSerial.onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, onUsbCdcEvent);
    USBSerial.onEvent(ARDUINO_USB_CDC_LINE_STATE_EVENT, onUsbCdcLineState);
    USBSerial.begin();
    USB.begin();
}

void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'd')
        {
            USBSerial.print("device to host");
        }
        else if (command == 'l')
        {
            Serial.printf("DEVICE_LINE_CODING seen=%u baud=%lu stop=%u parity=%u data=%u\n",
                          lineCodingSeen ? 1 : 0,
                          static_cast<unsigned long>(lineCodingBaud),
                          lineCodingStopBits,
                          lineCodingParity,
                          lineCodingDataBits);
        }
        else if (command == 'k')
        {
            printLineState();
        }
        else if (command == 'z')
        {
            // Clears the count and history but keeps the current state.
            portENTER_CRITICAL(&lineStateMux);
            lineStateCount = 0;
            portEXIT_CRITICAL(&lineStateMux);
            Serial.println("DEVICE_LINE_STATE_CLEARED");
        }
    }

    while (USBSerial.available() > 0)
    {
        Serial.print("DEVICE_RX ");
        while (USBSerial.available() > 0)
        {
            Serial.write(USBSerial.read());
        }
        Serial.println();
    }
    delay(1);
}
