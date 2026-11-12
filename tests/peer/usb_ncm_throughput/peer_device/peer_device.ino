#include "EspUsbDevice.h"
#include <NetworkServer.h>
#include <NetworkClient.h>

// Peer device for the CDC-NCM throughput / soak host test. Brings up a USB
// network device (CDC-NCM) with a DHCP server at 192.168.7.1, plus:
//   port 9000: sink   - reads and discards everything the host sends
//   port 9001: source - writes as fast as the host reads
//
// configure() below raises the IN NTB size (dwNtbInMaxSize) above the host's
// fixed 3200 byte reassembly buffer, which is legal for the device: the host
// never issues SET_NTB_INPUT_SIZE, so the device keeps its own advertised
// maximum. Three IN NTBs of up to eight datagrams each, as before.
static constexpr size_t NTB_IN_BYTES = 8192;

EspUsbDevice device;
EspUsbDeviceNet net(device);

NetworkServer sink(9000);
NetworkServer source(9001);
NetworkClient sinkClient;
NetworkClient sourceClient;

static uint8_t buffer[8192];
static uint32_t sinkBytes = 0;
static uint32_t sourceBytes = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspUsbNetConfig netConfig = net.configuration();
  netConfig.ntbInBytes = NTB_IN_BYTES;
  netConfig.txBuffers = 3;
  netConfig.maxDatagrams = 8;
  if (net.configure(netConfig) != ESP_OK)
  {
    Serial.println("NET_CONFIGURE 0");
    return;
  }

  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4032;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm-peer";

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN 0 %s\n", device.lastErrorName());
    return;
  }
  if (!net.beginNetwork())
  {
    Serial.println("NET_BEGIN 0");
    return;
  }

  memset(buffer, 0xa5, sizeof(buffer));
  sink.begin();
  sink.setNoDelay(true);
  source.begin();
  source.setNoDelay(true);

  Serial.printf("DEVICE_BEGIN 1 ip=%s ntbIn=%u\n",
                net.localIP().toString().c_str(),
                static_cast<unsigned>(net.configuration().ntbInBytes));
}

void loop()
{
  if (!sinkClient || !sinkClient.connected())
  {
    NetworkClient incoming = sink.accept();
    if (incoming)
    {
      sinkClient = incoming;
      sinkBytes = 0;
    }
  }
  if (sinkClient && sinkClient.connected())
  {
    while (sinkClient.available() > 0)
    {
      const int read = sinkClient.read(buffer, 1460);
      if (read <= 0)
      {
        break;
      }
      sinkBytes += static_cast<uint32_t>(read);
    }
  }

  if (!sourceClient || !sourceClient.connected())
  {
    NetworkClient incoming = source.accept();
    if (incoming)
    {
      sourceClient = incoming;
      sourceClient.setNoDelay(true);
      sourceBytes = 0;
    }
  }
  if (sourceClient && sourceClient.connected())
  {
    // Write several MSS worth at once so lwIP hands the NCM driver a burst of
    // datagrams and it batches them into one large NTB, the way a real USB NIC
    // does under load.
    const int written = sourceClient.write(buffer, sizeof(buffer));
    if (written > 0)
    {
      sourceBytes += static_cast<uint32_t>(written);
    }
  }

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      // link= is the host's SET_CONFIGURATION, which the tests wait for. Not
      // net.linkUp(): from 3.x that turns true only once the host selects the
      // data interface's alternate, i.e. after it attaches the network.
      Serial.printf("DEVICE_READY ip=%s link=%u ntbIn=%u\n",
                    net.localIP().toString().c_str(),
                    device.ready() ? 1 : 0,
                    static_cast<unsigned>(net.configuration().ntbInBytes));
    }
    else if (command == 'c')
    {
      Serial.printf("DEVICE_COUNTS sink=%lu source=%lu\n",
                    static_cast<unsigned long>(sinkBytes),
                    static_cast<unsigned long>(sourceBytes));
    }
  }
}
