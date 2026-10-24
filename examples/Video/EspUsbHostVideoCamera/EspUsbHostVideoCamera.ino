// en: Select a UVC stream and hand completed frames to foreground processing.
// ja: UVC streamを選び、完成frameをメイン処理へ渡す。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static uint8_t cameraAddress = 0;
static bool startRequested = false;

static uint8_t *frameCopy = nullptr;
static size_t frameCapacity = 0;
static volatile size_t pendingLength = 0;
static volatile bool pending = false;

static uint32_t framesDelivered = 0;
static uint32_t framesDropped = 0;
static uint32_t framesIncomplete = 0;
static uint32_t lastPrintMs = 0;
static bool markersReported = false;

static void handleVideoFrame(const EspUsbHostVideoFrame &frame)
{
  // en: An incomplete frame lost payloads on the way in; skip it so the printed rate is of whole frames.
  // ja: 不完全なフレームは途中で payload を落としているため、表示するレートを完全なフレームだけにするよう読み飛ばします。
  if (!frame.complete)
  {
    framesIncomplete++;
    return;
  }

  // en: Frames arrive on the USB task, which also resubmits the streaming transfers, so copy and return.
  // ja: フレームはUSBタスクで届き、そのタスクがストリーミング転送の再投入も行うため、コピーしてすぐ戻ります。
  if (pending || !frameCopy || frame.length > frameCapacity)
  {
    framesDropped++;
    return;
  }
  memcpy(frameCopy, frame.data, frame.length);
  pendingLength = frame.length;
  pending = true;
}

static bool selectVideoStream(uint8_t address)
{
  EspUsbHostVideoStreamInfo streams[ESP_USB_HOST_MAX_VIDEO_STREAMS];
  const size_t count = usb.getVideoStreams(address, streams, ESP_USB_HOST_MAX_VIDEO_STREAMS);

  // en: Print every parsed format/frame pair, then select the largest the camera offers.
  // ja: 解析済みの format/frame の組をすべて表示し、カメラが提示する中で最大のものを選びます。
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(streams[i]);
  }

  // en: 0 means no preference. Naming a format, size or rate the camera lacks fails instead of substituting another.
  // ja: 0は「指定なし」です。カメラが持たないフォーマット・サイズ・レートを指定した場合は、代替されず失敗します。
  const EspUsbHostVideoStreamSelection selected =
      espUsbHostSelectVideoStream(streams, count, ESP_USB_HOST_VIDEO_FORMAT_UNKNOWN, 0, 0, 0);
  if (!selected)
  {
    return false;
  }

  // en: dwMaxVideoFrameBufferSize is the camera's own bound for this format; width * height * bytes is wrong for MJPEG.
  // ja: フレームバッファはこのフォーマットについてカメラ自身が宣言した dwMaxVideoFrameBufferSize で確保します(MJPEGでは幅×高さ×バイト数は誤りです)。
  frameCapacity = streams[selected.index].maxVideoFrameBufferSize;
  frameCopy = static_cast<uint8_t *>(malloc(frameCapacity));
  if (!frameCopy)
  {
    Serial.printf("frame buffer alloc failed: %u bytes\n", static_cast<unsigned>(frameCapacity));
    return false;
  }

  if (!usb.videoStart(streams[selected.index], 0, address))
  {
    Serial.printf("videoStart failed: %s\n", usb.lastErrorName());
    free(frameCopy);
    frameCopy = nullptr;
    return false;
  }

  EspUsbHostVideoProbeControl commit;
  usb.videoCommitted(commit, address);
  Serial.printf("video selected: addr=%u iface=%u format=%s %ux%u fps=%lu payload=%lu frame_max=%lu\n",
                streams[selected.index].address,
                streams[selected.index].interfaceNumber,
                espUsbHostVideoFormatName(streams[selected.index].format),
                streams[selected.index].width,
                streams[selected.index].height,
                static_cast<unsigned long>(espUsbHostVideoFrameIntervalToFps(commit.frameInterval)),
                static_cast<unsigned long>(commit.maxPayloadTransferSize),
                static_cast<unsigned long>(frameCapacity));
  return true;
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost Video Camera example start");

  // en: A received frame pointer can be reused after return; hand off an owned copy.
  // ja: 受信frameのポインタは戻ると再利用され得る。自分のコピーを渡す。
  usb.onVideoFrame(handleVideoFrame);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(info);
                          // en: videoStart() waits on control transfers the USB task completes, so request it and start from loop().
                          // ja: videoStart()はUSBタスクが完了させるコントロール転送を待つため、ここでは要求だけ立ててloop()から開始します。
                          if (usb.getVideoStreamCount(info.address) > 0)
                          {
                            cameraAddress = info.address;
                            startRequested = true;
                          } });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &info)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(info);
                             if (info.address == cameraAddress)
                             {
                               cameraAddress = 0;
                               pending = false;
                               markersReported = false;
                               free(frameCopy);
                               frameCopy = nullptr;
                             } });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (startRequested)
  {
    startRequested = false;
    Serial.printf("video %s: addr=%u\n", selectVideoStream(cameraAddress) ? "ready" : "unsupported", cameraAddress);
  }

  if (pending)
  {
    const size_t length = pendingLength;

    // en: Decode, forward or store the frame here. An ESP32-P4's hardware JPEG decoder (driver/jpeg_decode.h) belongs in loop(), not in the callback.
    // ja: フレームのデコード・転送・保存はここで行います。ESP32-P4のハードウェアJPEGデコーダ(driver/jpeg_decode.h)もコールバックではなくloop()側です。
    const bool jpeg = length >= 4 &&
                      frameCopy[0] == 0xFF && frameCopy[1] == 0xD8 &&
                      frameCopy[length - 2] == 0xFF && frameCopy[length - 1] == 0xD9;
    if (!jpeg && !markersReported)
    {
      markersReported = true;
      Serial.println("frames carry no JPEG SOI/EOI markers (an uncompressed format, perhaps)");
    }
    framesDelivered++;

    // en: Release the slot only after the work is done, so a frame arriving meanwhile is dropped rather than torn.
    // ja: 処理を終えてからスロットを解放します。その間に届いたフレームは、壊れるのではなく破棄されます。
    pending = false;
  }

  const uint32_t now = millis();
  if (usb.videoStreaming(cameraAddress) && now - lastPrintMs >= 1000)
  {
    lastPrintMs = now;

    // en: Isochronous transfers are never retried, so these counters are the only sign of a stream that is losing data.
    // ja: isochronous転送は再送されないため、データを落としているストリームを知る手がかりはこれらのカウンタだけです。
    EspUsbHostVideoStats stats;
    usb.videoStats(stats, cameraAddress);
    Serial.printf("video: addr=%u fps=%3lu dropped=%lu incomplete=%lu packet_errors=%lu header_errors=%lu overflows=%lu\n",
                  cameraAddress,
                  static_cast<unsigned long>(framesDelivered),
                  static_cast<unsigned long>(framesDropped),
                  static_cast<unsigned long>(framesIncomplete),
                  static_cast<unsigned long>(stats.packetErrors),
                  static_cast<unsigned long>(stats.headerErrors),
                  static_cast<unsigned long>(stats.overflows));
    framesDelivered = 0;
    framesDropped = 0;
    framesIncomplete = 0;
  }
}
