// en: Decode USB MIDI input and send explicit test messages.
// ja: USB MIDI入力を解釈し、明示的な操作で試験メッセージを送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static const char *const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"};

static void printNote(uint8_t note)
{
  Serial.printf("%s%d", NOTE_NAMES[note % 12], (int)(note / 12) - 1);
}

static void printMidiMessage(const EspUsbHostMidiMessage &msg)
{
  // en: Decode the high nibble as the MIDI message type and the low nibble as channel.
  // ja: 上位4bitをMIDIメッセージ種別、下位4bitをチャンネルとして解釈します。
  uint8_t type = msg.status & 0xF0;
  uint8_t channel = (msg.status & 0x0F) + 1;

  switch (type)
  {
  case 0x80:
    Serial.printf("Note Off   ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" vel=%u\n", msg.data2);
    break;
  case 0x90:
    Serial.printf("Note On    ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" vel=%u\n", msg.data2);
    break;
  case 0xA0:
    Serial.printf("Aftertouch ch=%u note=", channel);
    printNote(msg.data1);
    Serial.printf(" pressure=%u\n", msg.data2);
    break;
  case 0xB0:
    Serial.printf("CC         ch=%u ctrl=%u val=%u\n", channel, msg.data1, msg.data2);
    break;
  case 0xC0:
    Serial.printf("Program    ch=%u prog=%u\n", channel, msg.data1);
    break;
  case 0xD0:
    Serial.printf("Pressure   ch=%u val=%u\n", channel, msg.data1);
    break;
  case 0xE0:
    Serial.printf("PitchBend  ch=%u val=%d\n", channel,
                  (int)(((uint16_t)msg.data2 << 7) | msg.data1) - 8192);
    break;
  default:
    Serial.printf("System     status=0x%02x data1=%u data2=%u\n",
                  msg.status, msg.data1, msg.data2);
    break;
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  Serial.println("n:NoteOn C4  f:NoteOff C4  c:CC#74  p:Program 10  b:PitchBend  s:SysEx");

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

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    { printMidiMessage(message); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    // en: Serial Monitor keys send a few common MIDI messages for quick testing.
    // ja: シリアルモニターのキー入力で、よく使うMIDIメッセージを送信して動作確認します。
    char command = Serial.read();
    if (command == 'n')
      usb.midiSendNoteOn(0, 60, 100);
    else if (command == 'f')
      usb.midiSendNoteOff(0, 60, 0);
    else if (command == 'c')
      usb.midiSendControlChange(0, 74, 64);
    else if (command == 'p')
      usb.midiSendProgramChange(0, 10);
    else if (command == 'b')
      usb.midiSendPitchBendSigned(0, 1024);
    else if (command == 's')
    {
      const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
      usb.midiSendSysEx(sysex, sizeof(sysex));
    }
  }
  delay(1);
}
