#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup()
{
    // Event prints can burst faster than the default serial TX buffer
    // drains; enlarge it so lines are not truncated mid-flight.
    Serial.setTxBufferSize(4096);
    Serial.begin(115200);
    delay(500);

    CdcSerial.begin(115200);

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
        if (command == 'h')
        {
            Serial.printf("SERIAL_TX %u\n", CdcSerial.write(reinterpret_cast<const uint8_t *>("host to serial\n"), 15) == 15 ? 1 : 0);
        }
        else if (command == 'c')
        {
            EspUsbHostSerialConfig config;
            config.baud = 57600;
            config.dataBits = 7;
            config.parity = ESP_USB_HOST_SERIAL_PARITY_EVEN;
            config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_2;
            Serial.printf("SERIAL_CONFIG %u\n", CdcSerial.setConfig(config) ? 1 : 0);
        }
        else if (command == 'm')
        {
            EspUsbHostSerialConfig config;
            config.baud = 300;
            config.dataBits = 5;
            config.parity = ESP_USB_HOST_SERIAL_PARITY_MARK;
            config.stopBits = ESP_USB_HOST_SERIAL_STOP_BITS_1_5;
            Serial.printf("SERIAL_CONFIG_MARK %u\n", CdcSerial.setConfig(config) ? 1 : 0);
        }
        else if (command == 'b')
        {
            Serial.printf("SERIAL_BAUD %u\n", CdcSerial.setBaudRate(115200) ? 1 : 0);
        }
        else if (command == 'q')
        {
            // Asynchronous CDC OUT queue. The pool now lives on the heap and is
            // owned by the port, so this walks the whole lifecycle: begin,
            // zero-copy acquire/submit, the copying async path, a plain Stream
            // write routing through the queue, flush, stats, and release.
            const uint8_t address = CdcSerial.address();
            const uint8_t port = CdcSerial.port();

            Serial.printf("QUEUE_BEGIN %u ready=%u free=%u\n",
                          usb.serialWriteQueueBegin(4, 128, address, port) ? 1 : 0,
                          usb.serialWriteQueueReady(address, port) ? 1 : 0,
                          static_cast<unsigned>(usb.serialWriteQueueFree(address, port)));

            size_t capacity = 0;
            uint8_t *buffer = usb.serialWriteAcquire(&capacity, 100, address, port);
            bool submitted = false;
            if (buffer)
            {
                memcpy(buffer, "QACQ ", 5);
                submitted = usb.serialWriteSubmit(buffer, 5, address, port);
            }
            Serial.printf("QUEUE_ACQUIRE %u capacity=%u submit=%u\n",
                          buffer ? 1 : 0,
                          static_cast<unsigned>(capacity),
                          submitted ? 1 : 0);

            Serial.printf("QUEUE_ASYNC %u\n",
                          usb.serialWriteAsync(reinterpret_cast<const uint8_t *>("QASYNC "), 7, 100, address, port) ? 1 : 0);
            Serial.printf("QUEUE_STREAM %u\n",
                          CdcSerial.write(reinterpret_cast<const uint8_t *>("QWRITE "), 7) == 7 ? 1 : 0);
            Serial.printf("QUEUE_FLUSH %u\n", usb.serialWriteFlush(1000, address, port) ? 1 : 0);

            const EspUsbHostSerialWriteStats stats = usb.serialWriteStats(address, port);
            Serial.printf("QUEUE_STATS submitted=%lu completed=%lu errors=%lu bytes=%lu pending=%u\n",
                          static_cast<unsigned long>(stats.submitted),
                          static_cast<unsigned long>(stats.completed),
                          static_cast<unsigned long>(stats.errors),
                          static_cast<unsigned long>(stats.bytes),
                          static_cast<unsigned>(usb.serialWritePending(address, port)));

            usb.serialWriteQueueEnd(address, port);
            Serial.printf("QUEUE_END ready=%u\n", usb.serialWriteQueueReady(address, port) ? 1 : 0);
            // The one-shot path has to keep working once the pool is gone.
            Serial.printf("QUEUE_AFTER %u\n",
                          CdcSerial.write(reinterpret_cast<const uint8_t *>("QAFTER "), 7) == 7 ? 1 : 0);
        }
        else if (command == 'r')
        {
            // Repeated begin/end of the queue. The pool is a heap block owned by
            // the port now, so a release path that failed to give it back shows
            // up as a heap delta here instead of as a slow leak in a long run.
            const uint8_t address = CdcSerial.address();
            const uint8_t port = CdcSerial.port();
            // Warm-up cycle first: the allocator can keep a fresh block after the
            // very first request of a size, which is not a leak but reads as one.
            usb.serialWriteQueueBegin(4, 128, address, port);
            usb.serialWriteQueueEnd(address, port);

            const uint32_t before = ESP.getFreeHeap();
            unsigned completed = 0;
            for (unsigned i = 0; i < 20; i++)
            {
                if (!usb.serialWriteQueueBegin(4, 128, address, port))
                {
                    break;
                }
                if (!usb.serialWriteAsync(reinterpret_cast<const uint8_t *>("QCYC "), 5, 100, address, port))
                {
                    break;
                }
                if (!usb.serialWriteFlush(1000, address, port))
                {
                    break;
                }
                usb.serialWriteQueueEnd(address, port);
                completed++;
            }
            const uint32_t after = ESP.getFreeHeap();
            Serial.printf("QUEUE_CYCLES completed=%u before=%lu after=%lu delta=%ld ready=%u\n",
                          completed,
                          static_cast<unsigned long>(before),
                          static_cast<unsigned long>(after),
                          static_cast<long>(after) - static_cast<long>(before),
                          usb.serialWriteQueueReady(address, port) ? 1 : 0);
        }
        else if (command == 'x')
        {
            usb.end();
            usb_host_lib_info_t info = {};
            const esp_err_t infoResult = usb_host_lib_info(&info);
            Serial.printf("HOST_END installed=%u clients=%d devices=%d ready=%u\n",
                          infoResult == ESP_OK ? 1 : 0,
                          infoResult == ESP_OK ? info.num_clients : -1,
                          infoResult == ESP_OK ? info.num_devices : -1,
                          usb.ready() ? 1 : 0);
            Serial.printf("HOST_REBEGIN %u\n", usb.begin() ? 1 : 0);
        }
    }
    if (CdcSerial.available() > 0)
    {
        Serial.print("SERIAL_RX ");
        while (CdcSerial.available() > 0)
        {
            Serial.write(CdcSerial.read());
        }
        Serial.println();
    }
    delay(1);
}
