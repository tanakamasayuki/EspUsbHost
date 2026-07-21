#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.onAudioOutputRequest([](EspUsbHostAudioOutputRequest &request)
                           { request.writtenFrames = 0; });
  usb.audioOutputStop();
  usb.begin();
}

void loop()
{
  delay(1);
}
