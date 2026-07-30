#include "EspUsbHost.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>

static constexpr uint8_t ADB_CLASS = 0xff;
static constexpr uint8_t ADB_SUBCLASS = 0x42;
static constexpr uint8_t ADB_PROTOCOL = 0x01;

static constexpr uint32_t A_CNXN = 0x4e584e43;
static constexpr uint32_t A_AUTH = 0x48545541;
static constexpr uint32_t A_OPEN = 0x4e45504f;
static constexpr uint32_t A_OKAY = 0x59414b4f;
static constexpr uint32_t A_CLSE = 0x45534c43;
static constexpr uint32_t A_WRTE = 0x45545257;

static constexpr uint32_t ADB_AUTH_TOKEN = 1;
static constexpr uint32_t ADB_AUTH_SIGNATURE = 2;
static constexpr uint32_t ADB_AUTH_RSAPUBLICKEY = 3;
static constexpr uint32_t A_VERSION = 0x01000000;
static constexpr uint32_t A_MAXDATA = 4096;
static constexpr uint32_t LOCAL_STREAM_ID = 1;
static constexpr uint32_t TEST_TIMEOUT_MS = 120000;
static constexpr uint32_t AUTHORIZATION_GRACE_MS = 30000;
static constexpr uint32_t STABILITY_MS = 2000;
static constexpr char SHELL_SERVICE[] = "shell:echo ESP_USB_HOST_ADB_OK";
static constexpr char EXPECTED_OUTPUT[] = "ESP_USB_HOST_ADB_OK";

class AdbKeyStore
{
public:
    AdbKeyStore()
    {
        mbedtls_pk_init(&key_);
    }

    ~AdbKeyStore()
    {
        mbedtls_pk_free(&key_);
    }

    bool begin()
    {
        if (load())
        {
            Serial.println("ADB key: loaded from NVS");
            return true;
        }

        Serial.println("ADB key: generating RSA-2048 key (first run only)...");
        mbedtls_pk_free(&key_);
        mbedtls_pk_init(&key_);
        if (mbedtls_pk_setup(&key_, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0)
        {
            return false;
        }

        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(key_);
        if (!rsa || mbedtls_rsa_gen_key(rsa, randomBytes, nullptr, 2048, 65537) != 0 ||
            mbedtls_rsa_check_privkey(rsa) != 0)
        {
            return false;
        }
        if (!save())
        {
            return false;
        }
        Serial.println("ADB key: generated and saved to NVS");
        return true;
    }

    bool signToken(const uint8_t *token, size_t length, uint8_t *signature, size_t capacity)
    {
        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(key_);
        if (!rsa || !token || length != 20 || !signature || capacity < 256)
        {
            return false;
        }
        return mbedtls_rsa_rsassa_pkcs1_v15_sign(rsa,
                                                 randomBytes,
                                                 nullptr,
                                                 MBEDTLS_MD_SHA1,
                                                 static_cast<unsigned int>(length),
                                                 token,
                                                 signature) == 0;
    }

    bool publicKey(uint8_t *output, size_t capacity, size_t &outputLength)
    {
        static constexpr size_t MODULUS_BYTES = 256;
        struct __attribute__((packed)) AndroidRsaPublicKey
        {
            uint32_t modulusSizeWords;
            uint32_t n0inv;
            uint8_t modulus[MODULUS_BYTES];
            uint8_t rr[MODULUS_BYTES];
            uint32_t exponent;
        } encoded = {};

        outputLength = 0;
        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(key_);
        if (!rsa || mbedtls_rsa_get_len(rsa) != MODULUS_BYTES)
        {
            return false;
        }

        mbedtls_mpi modulus;
        mbedtls_mpi exponent;
        mbedtls_mpi rr;
        mbedtls_mpi_init(&modulus);
        mbedtls_mpi_init(&exponent);
        mbedtls_mpi_init(&rr);

        bool ok = mbedtls_rsa_export(rsa, &modulus, nullptr, nullptr, nullptr, &exponent) == 0 &&
                  mbedtls_mpi_write_binary_le(&modulus, encoded.modulus, sizeof(encoded.modulus)) == 0 &&
                  mbedtls_mpi_lset(&rr, 1) == 0 &&
                  mbedtls_mpi_shift_l(&rr, MODULUS_BYTES * 16) == 0 &&
                  mbedtls_mpi_mod_mpi(&rr, &rr, &modulus) == 0 &&
                  mbedtls_mpi_write_binary_le(&rr, encoded.rr, sizeof(encoded.rr)) == 0;

        encoded.modulusSizeWords = MODULUS_BYTES / sizeof(uint32_t);
        const uint32_t modulusLowWord = static_cast<uint32_t>(encoded.modulus[0]) |
                                        (static_cast<uint32_t>(encoded.modulus[1]) << 8) |
                                        (static_cast<uint32_t>(encoded.modulus[2]) << 16) |
                                        (static_cast<uint32_t>(encoded.modulus[3]) << 24);
        uint32_t inverse = 1;
        for (uint8_t i = 0; i < 5; i++)
        {
            inverse *= 2 - modulusLowWord * inverse;
        }
        encoded.n0inv = 0 - inverse;
        encoded.exponent = 65537;

        mbedtls_mpi_free(&rr);
        mbedtls_mpi_free(&exponent);
        mbedtls_mpi_free(&modulus);
        if (!ok)
        {
            return false;
        }

        size_t base64Length = 0;
        if (mbedtls_base64_encode(output,
                                  capacity,
                                  &base64Length,
                                  reinterpret_cast<const uint8_t *>(&encoded),
                                  sizeof(encoded)) != 0)
        {
            return false;
        }

        static constexpr char COMMENT[] = " espusbhost@esp32";
        if (base64Length + sizeof(COMMENT) > capacity)
        {
            return false;
        }
        memcpy(output + base64Length, COMMENT, sizeof(COMMENT));
        outputLength = base64Length + sizeof(COMMENT);
        return true;
    }

private:
    static int randomBytes(void *, unsigned char *output, size_t length)
    {
        esp_fill_random(output, length);
        return 0;
    }

    bool load()
    {
        Preferences preferences;
        if (!preferences.begin("esp-adb", true))
        {
            return false;
        }
        const size_t length = preferences.getBytesLength("rsa-key");
        if (length == 0 || length > 1600)
        {
            preferences.end();
            return false;
        }

        uint8_t *der = static_cast<uint8_t *>(malloc(length));
        const bool read = der && preferences.getBytes("rsa-key", der, length) == length;
        preferences.end();
        if (!read)
        {
            free(der);
            return false;
        }

        const int parsed = mbedtls_pk_parse_key(&key_, der, length, nullptr, 0, randomBytes, nullptr);
        free(der);
        mbedtls_rsa_context *rsa = parsed == 0 ? mbedtls_pk_rsa(key_) : nullptr;
        return rsa && mbedtls_rsa_get_bitlen(rsa) == 2048 && mbedtls_rsa_check_privkey(rsa) == 0;
    }

    bool save()
    {
        uint8_t der[1600];
        const int length = mbedtls_pk_write_key_der(&key_, der, sizeof(der));
        if (length <= 0)
        {
            return false;
        }

        Preferences preferences;
        if (!preferences.begin("esp-adb", false))
        {
            return false;
        }
        const uint8_t *start = der + sizeof(der) - length;
        const bool saved = preferences.putBytes("rsa-key", start, length) == static_cast<size_t>(length);
        preferences.end();
        return saved;
    }

    mbedtls_pk_context key_;
};

EspUsbHost usb;
AdbKeyStore adbKey;

static volatile bool connectPending = false;
static uint8_t deviceAddress = 0;
static bool adbOpen = false;
static bool online = false;
static bool signatureSent = false;
static bool publicKeySent = false;
static bool publicKeyOffered = false;
static bool streamOpening = false;
static bool streamOpen = false;
static bool shellDone = false;
static bool finished = false;
static uint32_t remoteStreamId = 0;
static uint32_t signatureSentAtMs = 0;
static uint32_t publicKeySentAtMs = 0;
static uint32_t shellDoneAtMs = 0;
static portMUX_TYPE vendorRxMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t vendorRxRing[(A_MAXDATA + 24) * 2];
static size_t vendorRxHead = 0;
static size_t vendorRxTail = 0;
static size_t vendorRxCount = 0;
static volatile bool vendorRxOverflow = false;
static uint8_t rxBuffer[A_MAXDATA + 24];
static size_t rxLength = 0;
static char shellOutput[256];
static size_t shellOutputLength = 0;

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
                    const uint8_t *data = nullptr, uint32_t length = 0)
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
    if (length == 0)
    {
        return true;
    }
    if (!usb.vendorWrite(data, length, deviceAddress))
    {
        return false;
    }

    return true;
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
            if (!usb.vendorOpen(address, itf.number))
            {
                return false;
            }
            // ADB sends its header and payload as separate USB transfers. If one
            // ends exactly on a USB packet boundary, it must be terminated with a
            // ZLP so adbd does not consume the following ADB header as payload.
            usb.vendorSetAutoZlp(true, address);
            Serial.printf("ADB bulk OUT: ep=0x%02x mps=%u auto ZLP enabled\n",
                          usb.vendorOutEndpoint(address), usb.vendorOutPacketSize(address));
            return true;
        }
    }
    return false;
}

static void resetConnectionState()
{
    online = false;
    signatureSent = false;
    publicKeySent = false;
    streamOpening = false;
    streamOpen = false;
    shellDone = false;
    remoteStreamId = 0;
    signatureSentAtMs = 0;
    publicKeySentAtMs = 0;
    rxLength = 0;
    portENTER_CRITICAL(&vendorRxMux);
    vendorRxHead = 0;
    vendorRxTail = 0;
    vendorRxCount = 0;
    vendorRxOverflow = false;
    portEXIT_CRITICAL(&vendorRxMux);
    shellOutputLength = 0;
    shellOutput[0] = 0;
}

static void bufferVendorData(const EspUsbHostVendorData &event)
{
    if (!adbOpen || event.address != deviceAddress)
    {
        return;
    }

    portENTER_CRITICAL(&vendorRxMux);
    for (size_t i = 0; i < event.length; i++)
    {
        if (vendorRxCount == sizeof(vendorRxRing))
        {
            vendorRxOverflow = true;
            break;
        }
        vendorRxRing[vendorRxHead] = event.data[i];
        vendorRxHead = (vendorRxHead + 1) % sizeof(vendorRxRing);
        vendorRxCount++;
    }
    portEXIT_CRITICAL(&vendorRxMux);
}

static size_t drainVendorData(uint8_t *output, size_t capacity)
{
    size_t copied = 0;
    portENTER_CRITICAL(&vendorRxMux);
    while (copied < capacity && vendorRxCount > 0)
    {
        output[copied++] = vendorRxRing[vendorRxTail];
        vendorRxTail = (vendorRxTail + 1) % sizeof(vendorRxRing);
        vendorRxCount--;
    }
    portEXIT_CRITICAL(&vendorRxMux);
    return copied;
}

static void fail(const char *reason)
{
    if (!finished)
    {
        finished = true;
        Serial.printf("[FAIL] %s\n", reason);
    }
}

static bool sendConnect()
{
    static const char banner[] = "host::";
    Serial.println("ADB send: CNXN");
    return adbSend(A_CNXN, A_VERSION, A_MAXDATA,
                   reinterpret_cast<const uint8_t *>(banner), sizeof(banner));
}

static bool sendAuthSignature(const uint8_t *token, size_t length)
{
    uint8_t signature[256];
    if (!adbKey.signToken(token, length, signature, sizeof(signature)))
    {
        return false;
    }
    Serial.println("ADB send: AUTH SIGNATURE");
    return adbSend(A_AUTH, ADB_AUTH_SIGNATURE, 0, signature, sizeof(signature));
}

static bool sendAuthPublicKey()
{
    uint8_t publicKey[768];
    size_t length = 0;
    if (!adbKey.publicKey(publicKey, sizeof(publicKey), length))
    {
        return false;
    }
    Serial.println("ADB send: AUTH RSAPUBLICKEY");
    Serial.println("Approve the USB debugging dialog on the unlocked Android device.");
    publicKeySent = true;
    publicKeyOffered = true;
    publicKeySentAtMs = millis();
    return adbSend(A_AUTH, ADB_AUTH_RSAPUBLICKEY, 0, publicKey, length);
}

static bool openShell()
{
    Serial.printf("ADB send: OPEN %s\n", SHELL_SERVICE);
    streamOpening = true;
    return adbSend(A_OPEN,
                   LOCAL_STREAM_ID,
                   0,
                   reinterpret_cast<const uint8_t *>(SHELL_SERVICE),
                   sizeof(SHELL_SERVICE));
}

static void appendShellOutput(const uint8_t *data, size_t length)
{
    const size_t available = sizeof(shellOutput) - 1 - shellOutputLength;
    const size_t copyLength = length < available ? length : available;
    if (copyLength > 0)
    {
        memcpy(shellOutput + shellOutputLength, data, copyLength);
        shellOutputLength += copyLength;
        shellOutput[shellOutputLength] = 0;
    }
}

static void handleMessage(uint32_t command, uint32_t arg0, uint32_t arg1,
                          const uint8_t *payload, size_t length)
{
    if (command == A_AUTH)
    {
        if (arg0 != ADB_AUTH_TOKEN || length != 20)
        {
            fail("invalid AUTH TOKEN");
            return;
        }
        if (!signatureSent)
        {
            if (!sendAuthSignature(payload, length))
            {
                fail("AUTH SIGNATURE send failed");
            }
            else
            {
                signatureSent = true;
                signatureSentAtMs = millis();
            }
        }
        else if (!publicKeyOffered)
        {
            if (!sendAuthPublicKey())
            {
                fail("AUTH RSAPUBLICKEY send failed");
            }
        }
        else
        {
            Serial.println("ADB authorization is still pending on the Android device.");
        }
        return;
    }

    if (command == A_CNXN)
    {
        online = true;
        Serial.printf("ADB connected: version=0x%08x maxdata=%u\n",
                      static_cast<unsigned>(arg0), static_cast<unsigned>(arg1));
        if (!openShell())
        {
            fail("OPEN send failed");
        }
        return;
    }

    if (command == A_OKAY && streamOpening && arg1 == LOCAL_STREAM_ID)
    {
        remoteStreamId = arg0;
        streamOpening = false;
        streamOpen = true;
        Serial.printf("ADB stream open: local=%u remote=%u\n",
                      static_cast<unsigned>(LOCAL_STREAM_ID),
                      static_cast<unsigned>(remoteStreamId));
        return;
    }

    if (command == A_WRTE && streamOpen && arg0 == remoteStreamId && arg1 == LOCAL_STREAM_ID)
    {
        appendShellOutput(payload, length);
        Serial.print("ADB stream data: ");
        Serial.write(payload, length);
        if (length == 0 || payload[length - 1] != '\n')
        {
            Serial.println();
        }
        if (!adbSend(A_OKAY, LOCAL_STREAM_ID, remoteStreamId))
        {
            fail("OKAY send failed");
        }
        return;
    }

    if (command == A_CLSE && (streamOpening || streamOpen) &&
        (arg1 == LOCAL_STREAM_ID || arg1 == 0))
    {
        Serial.println("ADB stream closed");
        if (remoteStreamId != 0)
        {
            adbSend(A_CLSE, LOCAL_STREAM_ID, remoteStreamId);
        }
        streamOpening = false;
        streamOpen = false;
        remoteStreamId = 0;
        if (strstr(shellOutput, EXPECTED_OUTPUT))
        {
            shellDone = true;
            shellDoneAtMs = millis();
        }
        else
        {
            fail("shell output marker not received");
        }
    }
}

static void processReceived()
{
    while (rxLength >= 24 && !finished)
    {
        const uint32_t command = getU32(rxBuffer + 0);
        const uint32_t arg0 = getU32(rxBuffer + 4);
        const uint32_t arg1 = getU32(rxBuffer + 8);
        const uint32_t dataLength = getU32(rxBuffer + 12);
        const uint32_t checksum = getU32(rxBuffer + 16);
        const uint32_t magic = getU32(rxBuffer + 20);
        if (magic != (command ^ 0xffffffff))
        {
            fail("ADB header magic mismatch");
            return;
        }
        if (dataLength > A_MAXDATA)
        {
            fail("ADB payload exceeds negotiated maxdata");
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

        handleMessage(command, arg0, arg1, rxBuffer + 24, dataLength);
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

    if (!adbKey.begin())
    {
        fail("ADB key initialization failed");
        return;
    }

    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address, device.vid, device.pid, device.product);
        if (!adbOpen && openAdbInterface(device.address))
        {
            deviceAddress = device.address;
            adbOpen = true;
            resetConnectionState();
            connectPending = true;
        } });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
        Serial.printf("disconnected address=%u\n", device.address);
        if (device.address == deviceAddress)
        {
            adbOpen = false;
            connectPending = false;
            deviceAddress = 0;
            resetConnectionState();
        } });

    usb.onVendorData([](const EspUsbHostVendorData &event)
                     { bufferVendorData(event); });

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
        if (!sendConnect())
        {
            fail("CNXN send failed");
        }
    }

    if (!finished && vendorRxOverflow)
    {
        fail("ADB receive ring overflow");
    }

    if (!finished && adbOpen && rxLength < sizeof(rxBuffer))
    {
        const size_t received = drainVendorData(rxBuffer + rxLength,
                                                sizeof(rxBuffer) - rxLength);
        if (received > 0)
        {
            rxLength += received;
            processReceived();
        }
    }

    // Some devices do not send the second AUTH TOKEN described by the ADB
    // handshake. Offer the key once after a quiet period; never repeat it on
    // every USB re-enumeration because that can create a reconnect loop.
    if (!finished && adbOpen && signatureSent && !publicKeySent &&
        !publicKeyOffered && !online &&
        millis() - signatureSentAtMs >= 1500)
    {
        if (!sendAuthPublicKey())
        {
            fail("AUTH RSAPUBLICKEY fallback send failed");
        }
    }

    // After the key has been offered, wait for approval or for a re-enumerated
    // transport to accept its signature. A missing dialog is reported clearly
    // instead of continually resending the public key.
    if (!finished && adbOpen && publicKeyOffered && !online)
    {
        const uint32_t waitingSince = publicKeySent
                                          ? publicKeySentAtMs
                                          : signatureSentAtMs;
        if (waitingSince != 0 && millis() - waitingSince >= AUTHORIZATION_GRACE_MS)
        {
            fail("USB debugging authorization was not granted; revoke Android USB debugging authorizations and retry while unlocked");
        }
    }

    if (!finished && shellDone && adbOpen && millis() - shellDoneAtMs >= STABILITY_MS)
    {
        finished = true;
        Serial.printf("shell output: %s", shellOutput);
        if (shellOutputLength == 0 || shellOutput[shellOutputLength - 1] != '\n')
        {
            Serial.println();
        }
        Serial.println("[PASS]");
    }

    if (!finished && millis() - startedAtMs > TEST_TIMEOUT_MS)
    {
        fail("ADB authentication or shell timeout");
    }

    delay(10);
}
