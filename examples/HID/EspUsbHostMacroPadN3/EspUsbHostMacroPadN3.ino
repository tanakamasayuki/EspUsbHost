// en: Render LCD key images and receive macro-pad controls.
// ja: LCDキー画像を描き、マクロパッドの操作を受け取る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。


#include "EspUsbHost.h"
#include "KeyImage.hpp"
#include "MacroPadN3Device.hpp"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;
n3::MacroPadN3Device pad(usb);
n3::KeyImageEncoder image;

// Backlight, 0..100.
static constexpr uint8_t BRIGHTNESS = 80;

static volatile bool deviceArrived = false;
static bool padReady = false;
static uint32_t arrivedAt = 0;

// en: The session has to be held open or the pad drops off the bus. The vendor application sends its keepalive about every ten seconds; half of that leaves room for a packet to be missed.
// ja: sessionを保つkeepaliveが必要。未送信や切断を検出して接続状態を見直す。
static constexpr uint32_t KEEPALIVE_INTERVAL_MS = 5000;
static uint32_t lastKeepaliveAt = 0;

// One colour per LCD key, so which key ends up where is visible at a glance.
static const uint32_t KEY_COLORS[n3::KEY_COUNT] = {
    0xd62828, 0xf77f00, 0xfcbf49, 0x2a9d8f, 0x264653, 0x8e44ad};

static void paintKey(uint8_t keyIndex, uint32_t color)
{
  const uint16_t body = n3::KeyImageEncoder::rgb565((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
  const uint16_t border = n3::KeyImageEncoder::rgb565(0xff, 0xff, 0xff);

  image.fill(body);
  // en: Deliberately asymmetric, so one look at the panel says whether KEY_IMAGE_ROTATION is right: a horizontal bar near the top whose length counts the key, and a square in the bottom-left corner. Sideways bars mean the rotation is a quarter turn out; a bar along the bottom means it is 180 out.
  // ja: 非対称の模様を描き、回転の方向やキー番号を見た目で確認できるようにする。
  image.fillRect(0, 0, n3::KEY_IMAGE_WIDTH, 2, border);
  image.fillRect(0, n3::KEY_IMAGE_HEIGHT - 2, n3::KEY_IMAGE_WIDTH, 2, border);
  image.fillRect(0, 0, 2, n3::KEY_IMAGE_HEIGHT, border);
  image.fillRect(n3::KEY_IMAGE_WIDTH - 2, 0, 2, n3::KEY_IMAGE_HEIGHT, border);
  image.fillRect(6, 8, static_cast<uint16_t>(6 * keyIndex), 6, border);
  image.fillRect(6, n3::KEY_IMAGE_HEIGHT - 16, 10, 10, border);

  const uint8_t *jpeg = nullptr;
  size_t length = 0;
  if (!image.encode(jpeg, length))
  {
    Serial.printf("key %u: JPEG encode failed\n", keyIndex);
    return;
  }
  const bool ok = pad.setKeyImage(keyIndex, jpeg, length);
  Serial.printf("key %u: %u byte JPEG %s\n", keyIndex, (unsigned)length, ok ? "sent" : "FAILED");
}

static bool connect()
{
  if (!pad.begin())
  {
    return false;
  }
  Serial.printf("pad ready address=%u interface=%u\n", pad.address(), pad.interfaceNumber());

  // en: Proof that the device speaks this protocol, before anything is sent to it. The first field is the protocol version: this example assumes V3 packet sizes.
  // ja: 先に識別情報とprotocol版を確かめ、対応するpacket形式か確認してから設定を送る。
  char version[32] = {};
  if (pad.readFirmwareVersion(version, sizeof(version)))
  {
    Serial.printf("firmware=\"%s\"\n", version);
  }
  else
  {
    Serial.println("firmware read failed - this may not be an N3 family pad");
  }

  pad.onInput([](const n3::InputEvent &event, const uint8_t *raw, size_t rawLength)
              {
    // en: Runs on the USB task, so this only prints. The raw bytes are printed next to the decoded event so that a control this table does not cover is still visible.
    // ja: USBタスク上の通知なので短い表示に留め、生データと解釈した操作を対応付ける。
    if (event.keyIndex != 0)
    {
      Serial.printf("key %u %s", event.keyIndex, event.pressed ? "down" : "up");
    }
    else if (event.sceneKey != 0)
    {
      Serial.printf("scene key %u %s", event.sceneKey, event.pressed ? "down" : "up");
    }
    else if (event.encoder != 0 && event.delta != 0)
    {
      Serial.printf("encoder %u turn %+d", event.encoder, event.delta);
    }
    else if (event.encoder != 0)
    {
      Serial.printf("encoder %u %s", event.encoder, event.pressed ? "down" : "up");
    }
    else
    {
      Serial.printf("unknown code=0x%02x state=0x%02x", event.code, event.state);
    }
    Serial.print(" raw=");
    for (size_t i = 0; i < rawLength && i < 16; i++)
    {
      Serial.printf("%02x", raw[i]);
    }
    Serial.println(); });

  // en: Take the screens over and start the input reports. Everything below depends on this, and so does the keepalive in loop().
  // ja: 画面sessionと入力reportを開始する。以後の描画とkeepaliveは開始成功を前提にする。
  if (!pad.beginSession(BRIGHTNESS))
  {
    Serial.println("session start failed");
    return false;
  }
  lastKeepaliveAt = millis();
  pad.clearAll();

  if (!image.begin())
  {
    Serial.println("JPEG encoder unavailable");
    return false;
  }
  paintAllKeys();

  Serial.println("Press the keys and turn the encoders; events print below.");
  return true;
}

static void paintAllKeys()
{
  for (uint8_t key = 1; key <= n3::KEY_COUNT; key++)
  {
    paintKey(key, KEY_COLORS[key - 1]);
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(3000);
  Serial.println("MacroPad N3 example");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
    Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                  device.address, device.vid, device.pid, device.product);
    deviceArrived = true;
    arrivedAt = millis(); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
    Serial.printf("disconnected address=%u\n", device.address);
    padReady = false;
    pad.end(); });

  // The pad's own keyboard interface, for reference. Its keys report on the vendor
  // interface above, but the pad can also be configured to send keystrokes, and those
  // arrive here.
  // en: Handle input on the USB client task; copy any borrowed report data needed after return.
  // ja: 入力はUSB clientタスクで受ける。戻った後も使うreportデータはコピーする。
  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
    if (event.pressed)
    {
      Serial.printf("keyboard keycode=0x%02x modifiers=0x%02x\n", event.keycode, event.modifiers);
    } });

  EspUsbHostConfig cfg;
  // en: Both are required for this device. The high-speed port is the only one whose FIFO can hold a 1024-byte packet, and the default split caps interrupt OUT at 512 bytes, which makes claiming interface 0 fail with ESP_ERR_NOT_SUPPORTED.
  // ja: 必要なpacketを扱えるcontrollerとFIFO構成を選ぶ。claim失敗時は機器情報と資源を確認する。
  cfg.port = ESP_USB_HOST_PORT_HIGH_SPEED;
  cfg.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;
  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin(cfg))
  {
    Serial.printf("usb.begin() failed: %s\n", esp_err_to_name(usb.lastError()));
  }
}

void loop()
{
  // en: Enumeration finishes a moment after the connect callback, so the interface is looked up on a short delay rather than from inside it.
  // ja: 接続通知直後は準備中の場合がある。interfaceを使う処理は短い待ち合わせ後のアプリ経路へ置く。
  if (deviceArrived && !padReady && millis() - arrivedAt > 500)
  {
    deviceArrived = false;
    padReady = connect();
    if (!padReady)
    {
      Serial.println("no N3 family pad found on this device");
    }
  }

  if (padReady && millis() - lastKeepaliveAt >= KEEPALIVE_INTERVAL_MS)
  {
    lastKeepaliveAt = millis();
    pad.keepalive();
  }

  delay(10);
}
