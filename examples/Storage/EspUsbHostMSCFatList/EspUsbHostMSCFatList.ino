// en: Mount FAT storage, list files and perform a write/read/delete probe.
// ja: FATストレージをmountし、一覧と書き込み・読み戻し・削除を試す。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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
    // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
    // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
    Serial.begin(115200);
    delay(5000);

    // en: Device callbacks are only for logging; MSC mounting is handled in loop().
    // ja: デバイスコールバックはログ表示だけに使い、MSCのマウントはloop()側で行います。
    // en: Register the connection notification before startup; the reported address belongs to this enumeration.
    // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
    usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                          {
                              Serial.print("connected: ");
                              espUsbHostPrint(device); });

    // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
    // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
    usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                             {
                                 Serial.print("disconnected: ");
                                 espUsbHostPrint(device); });

    // en: Start host processing; success does not mean a peripheral has finished enumeration.
    // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }

    // Some non-compliant MSC devices, including DFMiniPlayer SD-card readers,
    // stall or become unusable when FatFs asks for SCSI SYNCHRONIZE CACHE(10).
    // Enable this before begin() mounts the device when using those readers.
    // usbMassStorage.setSkipSyncCache(true);
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
