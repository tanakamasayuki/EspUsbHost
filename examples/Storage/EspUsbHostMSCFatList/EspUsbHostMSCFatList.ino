#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostMscFS usbMassStorage;

static uint32_t lastMountAttemptMs = 0;

// en: List files through the Arduino fs::FS API, not through low-level MSC blocks.
// ja: 低レベルMSCブロックではなく、Arduinoのfs::FS APIでファイル一覧を読みます。
static void printRootEntries(fs::FS &fs)
{
    File root = fs.open("/");
    if (!root || !root.isDirectory())
    {
        Serial.println("open(/) failed");
        return;
    }

    Serial.println("Root entries:");
    while (true)
    {
        File entry = root.openNextFile();
        if (!entry)
        {
            break;
        }

        Serial.printf("  %s %s size=%u\n",
                      entry.isDirectory() ? "DIR " : "FILE",
                      entry.name(),
                      static_cast<unsigned>(entry.size()));
        entry.close();
    }
    root.close();
}

// en: Write, read back, and remove a small probe file to confirm basic file access.
// ja: 小さな確認用ファイルを書き込み、読み戻し、削除して基本的なファイルアクセスを確認します。
static void writeReadDeleteProbe(fs::FS &fs)
{
    const char *filePath = "/ESPUSBHT.TST";

    const char *message = "EspUsbHost MSC FAT write probe\n";
    File file = fs.open(filePath, FILE_WRITE);
    if (!file)
    {
        Serial.printf("open(%s, FILE_WRITE) failed\n", filePath);
        return;
    }
    const size_t written = file.print(message);
    file.close();
    Serial.printf("Wrote %u bytes to %s\n", static_cast<unsigned>(written), filePath);

    file = fs.open(filePath, FILE_READ);
    if (!file)
    {
        Serial.printf("open(%s, FILE_READ) failed\n", filePath);
        return;
    }
    char buffer[64] = {};
    const size_t readBytes = file.readBytes(buffer, sizeof(buffer) - 1);
    file.close();
    Serial.printf("Read back %u bytes: %s", static_cast<unsigned>(readBytes), buffer);

    if (fs.remove(filePath))
    {
        Serial.printf("Removed %s\n", filePath);
    }
    else
    {
        Serial.printf("remove(%s) failed\n", filePath);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(5000);

    // en: Device callbacks are only for logging; MSC mounting is handled in loop().
    // ja: デバイスコールバックはログ表示だけに使い、MSCのマウントはloop()側で行います。
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.print("connected: ");
                              espUsbHostPrint(device); });

    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.print("disconnected: ");
                                 espUsbHostPrint(device); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    // en: Mount once, then keep the filesystem available for other fs::FS users.
    // ja: 一度だけマウントし、その後は他のfs::FS利用コードから使えるように保持します。
    if (!usbMassStorage.mounted())
    {
        const uint32_t now = millis();

        // en: Retry mounting at a low rate so other loop work can continue.
        // ja: 他のloop処理を止めないよう、マウント再試行は低頻度にします。
        if (now - lastMountAttemptMs >= 1000)
        {
            lastMountAttemptMs = now;

            if (usbMassStorage.begin(usb, "/usb"))
            {
                printRootEntries(usbMassStorage);
                writeReadDeleteProbe(usbMassStorage);
            }
            else
            {
                Serial.printf("USB MSC FS mount failed: %s\n", usb.lastErrorName());
            }
        }
    }

    delay(10);
}
