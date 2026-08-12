#include "EspUsbHost.h"
#include <NetworkClient.h>

// Host side of the CDC-NCM throughput / soak peer test. Pairs with the
// EspUsbDevice NCM device in peer_device/, which runs a DHCP server at
// 192.168.7.1, a TCP sink on port 9000 and a TCP source on port 9001.
//
// The point of this test is sustained traffic, not enumeration: it drives the
// bulk OUT (host -> device) and bulk IN (device -> host) paths for seconds at a
// time and reports whether the data path keeps making progress.

EspUsbHost usb;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static bool attached = false;
static uint32_t lastReportedIp = 0;

static const uint16_t SINK_PORT = 9000;
static const uint16_t SOURCE_PORT = 9001;
static const IPAddress PEER_IP(192, 168, 7, 1);
static uint8_t buffer[1460];

static void reportStats(const char *tag)
{
  EspUsbHostNetworkStats st;
  usb.networkStats(st, deviceAddress);
  Serial.printf("%s ready=%u link=%u netif=%u rxNtb=%lu rxFrames=%lu tx=%lu txFail=%lu "
                "oversized=%lu ntbIn=%u heap=%lu block=%lu\n",
                tag,
                st.ready ? 1 : 0, st.linkUp ? 1 : 0, st.netifAttached ? 1 : 0,
                static_cast<unsigned long>(st.rxNtb),
                static_cast<unsigned long>(st.rxFrames),
                static_cast<unsigned long>(st.txFrames),
                static_cast<unsigned long>(st.txFails),
                static_cast<unsigned long>(st.rxOversized),
                static_cast<unsigned>(st.ntbInSize),
                static_cast<unsigned long>(esp_get_free_heap_size()),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

// Push data at the peer's TCP sink for durationMs and report the rate. This is
// the direction the reported WebSocket workload stresses: every Ethernet frame
// becomes one synchronous NTB on the bulk OUT endpoint.
static void txSoak(uint32_t durationMs)
{
  NetworkClient client;
  if (!client.connect(PEER_IP, SINK_PORT, 5000))
  {
    Serial.println("TX_SOAK connect=0");
    return;
  }
  client.setNoDelay(true);
  memset(buffer, 0x5a, sizeof(buffer));

  const uint32_t startMs = millis();
  uint32_t bytes = 0;
  uint32_t writeFails = 0;
  while (millis() - startMs < durationMs)
  {
    const int written = client.write(buffer, sizeof(buffer));
    if (written <= 0)
    {
      writeFails++;
      if (writeFails > 100)
      {
        break;
      }
      delay(1);
      continue;
    }
    bytes += static_cast<uint32_t>(written);
  }
  const uint32_t elapsed = millis() - startMs;
  client.stop();

  Serial.printf("TX_SOAK connect=1 bytes=%lu ms=%lu kbps=%lu writeFails=%lu\n",
                static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(elapsed ? (bytes * 8ULL / elapsed) : 0),
                static_cast<unsigned long>(writeFails));
  reportStats("TX_SOAK_STATS");
}

// Pull data from the peer's TCP source for durationMs. The device batches
// several datagrams into one NTB once it has a queue, so this is the direction
// that exercises device->host NTB reassembly.
static void rxSoak(uint32_t durationMs)
{
  NetworkClient client;
  if (!client.connect(PEER_IP, SOURCE_PORT, 5000))
  {
    Serial.println("RX_SOAK connect=0");
    return;
  }

  const uint32_t startMs = millis();
  uint32_t bytes = 0;
  uint32_t idleMs = 0;
  uint32_t maxIdleMs = 0;
  uint32_t lastDataMs = startMs;
  while (millis() - startMs < durationMs)
  {
    const int available = client.available();
    if (available > 0)
    {
      const int read = client.read(buffer, sizeof(buffer));
      if (read > 0)
      {
        bytes += static_cast<uint32_t>(read);
        lastDataMs = millis();
      }
    }
    else
    {
      if (!client.connected())
      {
        break;
      }
      idleMs = millis() - lastDataMs;
      if (idleMs > maxIdleMs)
      {
        maxIdleMs = idleMs;
      }
      delay(1);
    }
  }
  const uint32_t elapsed = millis() - startMs;
  client.stop();

  Serial.printf("RX_SOAK connect=1 bytes=%lu ms=%lu kbps=%lu maxIdleMs=%lu\n",
                static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(elapsed ? (bytes * 8ULL / elapsed) : 0),
                static_cast<unsigned long>(maxIdleMs));
  reportStats("RX_SOAK_STATS");
}

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connected = false;
                             attached = false;
                             lastReportedIp = 0;
                           });

  EspUsbHostConfig cfg;
#if defined(SOAK_TASK_PRIORITY)
  cfg.taskPriority = SOAK_TASK_PRIORITY;
#endif
  if (!usb.begin(cfg))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (attached)
  {
    const uint32_t ip = static_cast<uint32_t>(usb.networkLocalIP(deviceAddress));
    if (ip != 0 && ip != lastReportedIp)
    {
      lastReportedIp = ip;
      Serial.printf("NETWORK_IP ip=%s\n", IPAddress(ip).toString().c_str());
    }
  }

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'a')
    {
      EspUsbHostNetworkConfig netConfig; // dhcpClient = true
      attached = usb.networkAttachNetif(netConfig, deviceAddress);
      Serial.printf("NETWORK_ATTACH ok=%u\n", attached ? 1 : 0);
    }
    else if (command == 'd')
    {
      reportStats("NETWORK_STATS");
    }
    else if (command == 't')
    {
      txSoak(5000);
    }
    else if (command == 'r')
    {
      rxSoak(5000);
    }
  }
  delay(1);
}
