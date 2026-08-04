// Host tests for the USB Audio Class descriptor and control decoding helpers.
//
// This file is compiled and run on the host with g++ (no board required) by
// test_audio_uac.py, which extracts the helpers under test verbatim from
// src/EspUsbHost.h into "espusbhost_audio_real.h". The checks therefore run
// against the production decoders, not a copy.
//
// Descriptor bytes below follow the UAC1 (ADC 1.0) and UAC2 (ADC 2.0) class
// documents, and the UAC2 samples match what the sibling EspUsbDevice library
// emits, so the peer test and this test describe the same layouts.

#include "espusbhost_audio_real.h"

#include <cstdio>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
  if (!condition)
  {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

void checkEqual(unsigned long actual, unsigned long expected, const char *what)
{
  if (actual != expected)
  {
    printf("FAIL: %s (actual=%lu expected=%lu)\n", what, actual, expected);
    failures++;
  }
}

// Little-endian appenders for building descriptor and RANGE payloads.
void appendU16(std::vector<uint8_t> &out, uint16_t value)
{
  out.push_back(static_cast<uint8_t>(value & 0xff));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t> &out, uint32_t value)
{
  appendU16(out, static_cast<uint16_t>(value & 0xffff));
  appendU16(out, static_cast<uint16_t>(value >> 16));
}

// wNumSubRanges followed by MIN/MAX/RES triples of 4-byte values.
std::vector<uint8_t> sampleRateRange(const std::vector<std::vector<uint32_t>> &subRanges)
{
  std::vector<uint8_t> payload;
  appendU16(payload, static_cast<uint16_t>(subRanges.size()));
  for (const std::vector<uint32_t> &subRange : subRanges)
  {
    for (uint32_t value : subRange)
    {
      appendU32(payload, value);
    }
  }
  return payload;
}

// ---------------------------------------------------------------------------
// Feature Unit descriptor layout
// ---------------------------------------------------------------------------

void testFeatureUnitLayoutUac1()
{
  // UAC1 FEATURE_UNIT, 2 channels, bControlSize = 1:
  // bLength, CS_INTERFACE, FEATURE_UNIT, bUnitID, bSourceID, bControlSize,
  // bmaControls[0..2], iFeature.
  const std::vector<uint8_t> descriptor = {
      10, 0x24, 0x06, 2, 1, 1, 0x03, 0x03, 0x03, 0};
  const EspUsbHostAudioFeatureUnitLayout layout =
      espUsbHostAudioFeatureUnitLayout(descriptor.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1);
  check(layout.valid, "UAC1 feature unit layout is valid");
  checkEqual(layout.controlSize, 1, "UAC1 control size comes from bControlSize");
  checkEqual(layout.controlOffset, 6, "UAC1 bmaControls starts after bControlSize");
  checkEqual(layout.channelCount, 2, "UAC1 channel count excludes the master entry");

  // Same unit with a 2-byte bControlSize: bLength = 7 + 3 * 2.
  const std::vector<uint8_t> wide = {
      13, 0x24, 0x06, 2, 1, 2, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0};
  const EspUsbHostAudioFeatureUnitLayout wideLayout =
      espUsbHostAudioFeatureUnitLayout(wide.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1);
  check(wideLayout.valid, "UAC1 wide feature unit layout is valid");
  checkEqual(wideLayout.controlSize, 2, "UAC1 honours a 2-byte bControlSize");
  checkEqual(wideLayout.channelCount, 2, "UAC1 wide channel count");
}

void testFeatureUnitLayoutUac2()
{
  // UAC2 FEATURE_UNIT, 2 channels: bControlSize is gone and bmaControls is a
  // fixed 4 bytes per entry, so bLength = 6 + 3 * 4.
  std::vector<uint8_t> descriptor = {18, 0x24, 0x06, 2, 1};
  for (int entry = 0; entry < 3; entry++)
  {
    appendU32(descriptor, 0x0000000fU);
  }
  descriptor.push_back(0); // iFeature

  const EspUsbHostAudioFeatureUnitLayout layout =
      espUsbHostAudioFeatureUnitLayout(descriptor.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC2);
  check(layout.valid, "UAC2 feature unit layout is valid");
  checkEqual(layout.controlSize, 4, "UAC2 control size is fixed at 4");
  checkEqual(layout.controlOffset, 5, "UAC2 bmaControls starts right after bSourceID");
  checkEqual(layout.channelCount, 2, "UAC2 channel count excludes the master entry");

  // Reading the same bytes as UAC1 would find bControlSize = 0x0f and run off
  // the end, so the layout must be rejected rather than silently misparsed.
  const EspUsbHostAudioFeatureUnitLayout mismatched =
      espUsbHostAudioFeatureUnitLayout(descriptor.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1);
  check(!mismatched.valid, "a UAC2 feature unit is rejected when read as UAC1");

  // Master control only (0 channels): bLength = 6 + 4.
  std::vector<uint8_t> masterOnly = {10, 0x24, 0x06, 2, 1};
  appendU32(masterOnly, 0x0000000fU);
  masterOnly.push_back(0);
  const EspUsbHostAudioFeatureUnitLayout masterLayout =
      espUsbHostAudioFeatureUnitLayout(masterOnly.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC2);
  check(masterLayout.valid, "UAC2 master-only feature unit is valid");
  checkEqual(masterLayout.channelCount, 0, "UAC2 master-only unit has no channels");
}

void testFeatureUnitLayoutRejects()
{
  const std::vector<uint8_t> tooShort = {6, 0x24, 0x06, 2, 1, 1};
  check(!espUsbHostAudioFeatureUnitLayout(tooShort.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1).valid,
        "a truncated feature unit descriptor is rejected");

  const std::vector<uint8_t> zeroControlSize = {10, 0x24, 0x06, 2, 1, 0, 0, 0, 0, 0};
  check(!espUsbHostAudioFeatureUnitLayout(zeroControlSize.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1).valid,
        "bControlSize = 0 is rejected");

  const std::vector<uint8_t> hugeControlSize = {
      20, 0x24, 0x06, 2, 1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  check(!espUsbHostAudioFeatureUnitLayout(hugeControlSize.data(), ESP_USB_HOST_AUDIO_PROTOCOL_UAC1).valid,
        "a bControlSize wider than the 32-bit mask is rejected");
}

// ---------------------------------------------------------------------------
// Feature Unit bmaControls decoding
// ---------------------------------------------------------------------------

void testControlMasks()
{
  constexpr uint8_t kMute = 0x01;   // FU_MUTE_CONTROL
  constexpr uint8_t kVolume = 0x02; // FU_VOLUME_CONTROL
  constexpr uint8_t kBass = 0x03;   // FU_BASS_CONTROL
  constexpr uint8_t uac1 = ESP_USB_HOST_AUDIO_PROTOCOL_UAC1;
  constexpr uint8_t uac2 = ESP_USB_HOST_AUDIO_PROTOCOL_UAC2;

  // UAC1: one bit per control.
  check(espUsbHostAudioFeatureHasControl(0x03, kMute, uac1), "UAC1 D0 declares mute");
  check(espUsbHostAudioFeatureHasControl(0x03, kVolume, uac1), "UAC1 D1 declares volume");
  check(!espUsbHostAudioFeatureHasControl(0x03, kBass, uac1), "UAC1 D2 clear means no bass");
  check(espUsbHostAudioFeatureHasControl(0x02, kVolume, uac1), "UAC1 volume without mute");
  check(!espUsbHostAudioFeatureHasControl(0x02, kMute, uac1), "UAC1 mute bit clear");
  check(espUsbHostAudioFeatureControlWritable(0x03, kVolume, uac1),
        "UAC1 has no read-only encoding, so a declared control is writable");
  check(!espUsbHostAudioFeatureHasControl(0x03, 0, uac1), "control selector 0 is not a control");

  // UAC2: two bits per control, 01 = read-only, 11 = host-programmable.
  // 0x0f is what a mute+volume unit declares (0b11 in D1..D0 and D3..D2).
  check(espUsbHostAudioFeatureHasControl(0x0f, kMute, uac2), "UAC2 0x0f declares mute");
  check(espUsbHostAudioFeatureHasControl(0x0f, kVolume, uac2), "UAC2 0x0f declares volume");
  check(espUsbHostAudioFeatureControlWritable(0x0f, kMute, uac2), "UAC2 0x0f mute is writable");
  check(espUsbHostAudioFeatureControlWritable(0x0f, kVolume, uac2), "UAC2 0x0f volume is writable");
  check(!espUsbHostAudioFeatureHasControl(0x0f, kBass, uac2), "UAC2 0x0f declares no bass");

  // 0x05 is 01 in both fields: present but read-only.
  check(espUsbHostAudioFeatureHasControl(0x05, kMute, uac2), "UAC2 0x05 declares mute");
  check(espUsbHostAudioFeatureHasControl(0x05, kVolume, uac2), "UAC2 0x05 declares volume");
  check(!espUsbHostAudioFeatureControlWritable(0x05, kMute, uac2), "UAC2 0x05 mute is read-only");
  check(!espUsbHostAudioFeatureControlWritable(0x05, kVolume, uac2), "UAC2 0x05 volume is read-only");

  // Volume alone: 0x0c leaves the mute field zero.
  check(!espUsbHostAudioFeatureHasControl(0x0c, kMute, uac2), "UAC2 0x0c declares no mute");
  check(espUsbHostAudioFeatureHasControl(0x0c, kVolume, uac2), "UAC2 0x0c declares volume");

  // Reading a UAC2 mask with UAC1 rules would report bass from the volume field.
  check(espUsbHostAudioFeatureHasControl(0x0f, kBass, uac1),
        "UAC1 rules misread a UAC2 mask, which is why the protocol is tracked");
}

// ---------------------------------------------------------------------------
// Isochronous endpoint usage type
// ---------------------------------------------------------------------------

void testFeedbackEndpoint()
{
  check(espUsbHostAudioIsFeedbackEndpoint(0x11),
        "isochronous + usage type 01 is an explicit feedback endpoint");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x05),
        "isochronous asynchronous data is not feedback");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x09),
        "isochronous adaptive data is not feedback");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x0d),
        "isochronous synchronous data is not feedback");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x25),
        "usage type 10 is implicit feedback data and still carries audio");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x02), "bulk endpoints are never feedback");
  check(!espUsbHostAudioIsFeedbackEndpoint(0x13),
        "interrupt endpoints are never feedback");
}

// ---------------------------------------------------------------------------
// UAC2 RANGE responses
// ---------------------------------------------------------------------------

void testRangeSubRangeCount()
{
  // The 2-byte probe carries wNumSubRanges but no subranges.
  const std::vector<uint8_t> probe = {2, 0};
  checkEqual(espUsbHostAudioRangeDeclaredCount(probe.data(), probe.size()), 2,
             "the probe reports the declared subrange count");
  checkEqual(espUsbHostAudioRangeSubRangeCount(probe.data(), probe.size(), 12), 0,
             "the probe carries no complete subranges");

  const std::vector<uint8_t> full = sampleRateRange({{44100, 44100, 0}, {48000, 48000, 0}});
  checkEqual(espUsbHostAudioRangeSubRangeCount(full.data(), full.size(), 12), 2,
             "a full response reports every subrange");

  // A short wLength truncates the payload; the complete entries still count.
  checkEqual(espUsbHostAudioRangeSubRangeCount(full.data(), 2 + 12, 12), 1,
             "a truncated response reports only the complete subranges");

  const std::vector<uint8_t> empty = {0, 0};
  checkEqual(espUsbHostAudioRangeDeclaredCount(empty.data(), empty.size()), 0,
             "wNumSubRanges = 0 declares nothing");
  checkEqual(espUsbHostAudioRangeSubRangeCount(nullptr, 0, 12), 0, "a null payload counts as none");
}

void testDecodeSampleRateRange()
{
  uint32_t rates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};

  // Discrete rates are encoded as MIN == MAX with RES = 0, which is what
  // EspUsbDevice's UAC2 clock source returns.
  const std::vector<uint8_t> discrete = sampleRateRange({{48000, 48000, 0}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(discrete.data(), discrete.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             1, "a single discrete rate decodes to one rate");
  checkEqual(rates[0], 48000, "the discrete rate is 48000");

  const std::vector<uint8_t> two = sampleRateRange({{44100, 44100, 0}, {48000, 48000, 0}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(two.data(), two.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             2, "two discrete subranges decode to two rates");
  checkEqual(rates[0], 44100, "first rate");
  checkEqual(rates[1], 48000, "second rate");

  // A continuous subrange contributes its endpoints and the steps between.
  const std::vector<uint8_t> continuous = sampleRateRange({{8000, 16000, 4000}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(continuous.data(), continuous.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             3, "a continuous subrange walks its resolution");
  checkEqual(rates[0], 8000, "continuous minimum");
  checkEqual(rates[1], 12000, "continuous step");
  checkEqual(rates[2], 16000, "continuous maximum");

  // Without a resolution only the endpoints are known.
  const std::vector<uint8_t> unstepped = sampleRateRange({{8000, 96000, 0}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(unstepped.data(), unstepped.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             2, "a continuous subrange without resolution yields its endpoints");
  checkEqual(rates[0], 8000, "unstepped minimum");
  checkEqual(rates[1], 96000, "unstepped maximum");

  // Duplicates across subranges collapse.
  const std::vector<uint8_t> duplicated =
      sampleRateRange({{48000, 48000, 0}, {48000, 48000, 0}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(duplicated.data(), duplicated.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             1, "a repeated rate is only reported once");

  // More rates than the array holds are clamped, never overrun.
  const std::vector<uint8_t> many = sampleRateRange({{32000, 32000, 0},
                                                     {44100, 44100, 0},
                                                     {48000, 48000, 0},
                                                     {88200, 88200, 0},
                                                     {96000, 96000, 0}});
  uint32_t clamped[3] = {};
  checkEqual(espUsbHostAudioDecodeSampleRateRange(many.data(), many.size(), clamped, 3), 3,
             "decoding stops at the caller's capacity");
  checkEqual(clamped[2], 48000, "the first rates that fit are kept");

  // A zero rate is not a usable rate.
  const std::vector<uint8_t> zeroed = sampleRateRange({{0, 0, 0}});
  checkEqual(espUsbHostAudioDecodeSampleRateRange(zeroed.data(), zeroed.size(), rates,
                                                  ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES),
             0, "a zero rate is dropped");

  checkEqual(espUsbHostAudioDecodeSampleRateRange(discrete.data(), discrete.size(), rates, 0), 0,
             "a zero-capacity request decodes nothing");
}

void testDecodeVolumeRange()
{
  // wNumSubRanges = 1, then MIN/MAX/RES as signed 16-bit 1/256 dB values.
  // -2560 = -10 dB, 256 = 1 dB resolution.
  std::vector<uint8_t> payload;
  appendU16(payload, 1);
  appendU16(payload, static_cast<uint16_t>(-2560));
  appendU16(payload, 0);
  appendU16(payload, 256);

  EspUsbHostAudioVolumeRange range;
  check(espUsbHostAudioDecodeVolumeRange(payload.data(), payload.size(), range),
        "a one-subrange volume response decodes");
  checkEqual(static_cast<uint16_t>(range.min), 0xf600, "volume minimum is -2560 (0xf600)");
  checkEqual(static_cast<unsigned long>(range.max), 0, "volume maximum is 0");
  checkEqual(static_cast<unsigned long>(range.resolution), 256, "volume resolution is 256");

  EspUsbHostAudioVolumeRange rejected;
  check(!espUsbHostAudioDecodeVolumeRange(payload.data(), 2, rejected),
        "a probe-sized volume response carries no subrange");
  const std::vector<uint8_t> none = {0, 0, 0, 0, 0, 0, 0, 0};
  check(!espUsbHostAudioDecodeVolumeRange(none.data(), none.size(), rejected),
        "wNumSubRanges = 0 is rejected");
}

void testReadU32()
{
  const std::vector<uint8_t> bytes = {0x80, 0xbb, 0x00, 0x00};
  checkEqual(espUsbHostAudioReadU32(bytes.data()), 48000, "48000 decodes little-endian");
  const std::vector<uint8_t> high = {0x00, 0x00, 0x00, 0x01};
  checkEqual(espUsbHostAudioReadU32(high.data()), 0x01000000UL, "the top byte is the most significant");
}


// ---------------------------------------------------------------------------
// Format selection
// ---------------------------------------------------------------------------

// A stream as getAudioStreams() would report it. Rates are the discrete list.
EspUsbHostAudioStreamInfo pcmStream(bool input,
                                   uint8_t channels,
                                   uint8_t bitsPerSample,
                                   const std::vector<uint32_t> &rates,
                                   uint8_t alternate = 1)
{
  EspUsbHostAudioStreamInfo stream;
  stream.address = 1;
  stream.interfaceNumber = input ? 2 : 1;
  stream.alternate = alternate;
  stream.endpointAddress = input ? 0x81 : 0x01;
  stream.input = input;
  stream.output = !input;
  stream.channels = channels;
  stream.bitsPerSample = bitsPerSample;
  stream.bytesPerSample = static_cast<uint8_t>((bitsPerSample + 7) / 8);
  stream.sampleRateCount = static_cast<uint8_t>(rates.size());
  for (size_t i = 0; i < rates.size() && i < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++)
  {
    stream.sampleRates[i] = rates[i];
  }
  stream.sampleRate = rates.empty() ? 0 : rates[0];
  return stream;
}

void testSelectExactFormat()
{
  const std::vector<EspUsbHostAudioStreamInfo> streams = {
      pcmStream(false, 1, 16, {48000}),
      pcmStream(true, 1, 16, {48000}),
  };

  const EspUsbHostAudioStreamSelection out =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 1, 16, 48000);
  check(static_cast<bool>(out), "a fully specified output format resolves");
  checkEqual(out.index, 0, "the output stream is selected for an output request");
  checkEqual(out.sampleRate, 48000, "the requested rate is used");

  const EspUsbHostAudioStreamSelection in =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), true, 1, 16, 48000);
  checkEqual(in.index, 1, "the input stream is selected for an input request");

  const EspUsbHostAudioStreamSelection mismatch =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 2, 16, 48000);
  check(!mismatch, "a channel count the device does not offer does not resolve");
  const EspUsbHostAudioStreamSelection rateMismatch =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 1, 16, 96000);
  check(!rateMismatch, "a rate the device does not offer does not resolve");
}

void testSelectWildcards()
{
  // A device that splits formats across alternates, as most USB DACs do.
  const std::vector<EspUsbHostAudioStreamInfo> streams = {
      pcmStream(false, 1, 16, {32000}, 1),
      pcmStream(false, 2, 24, {48000}, 2),
      pcmStream(false, 2, 16, {44100, 48000}, 3),
  };

  const EspUsbHostAudioStreamSelection best =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 0, 0, 0);
  check(static_cast<bool>(best), "an unspecified format resolves");
  checkEqual(best.index, 2, "48 kHz 16-bit stereo outranks 24-bit and 32 kHz");
  checkEqual(best.sampleRate, 48000, "48 kHz is preferred over 44.1 kHz");

  const EspUsbHostAudioStreamSelection highRes =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 0, 24, 0);
  checkEqual(highRes.index, 1, "pinning the sample width selects the 24-bit alternate");

  const EspUsbHostAudioStreamSelection mono =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 1, 0, 0);
  checkEqual(mono.index, 0, "pinning the channel count selects the mono alternate");
  checkEqual(mono.sampleRate, 32000, "the mono alternate only offers 32 kHz");

  // A pinned rate wins over the scoring preference for another rate.
  const EspUsbHostAudioStreamSelection pinned =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 0, 0, 44100);
  checkEqual(pinned.index, 2, "44.1 kHz is only offered by one alternate");
  checkEqual(pinned.sampleRate, 44100, "the pinned rate is used, not the higher-scoring one");
}

void testSelectSkipsFormatOnlyStreams()
{
  // The 24-bit alternate is the one the host claimed; the others are format-only.
  std::vector<EspUsbHostAudioStreamInfo> streams = {
      pcmStream(false, 2, 16, {48000}, 1),
      pcmStream(false, 2, 24, {48000}, 2),
  };
  streams[0].startable = false;

  const EspUsbHostAudioStreamSelection best =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 0, 0, 0);
  checkEqual(best.index, 1, "a format-only alternate is not selected even when it scores higher");

  const EspUsbHostAudioStreamSelection exact =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 2, 16, 48000);
  check(!exact, "asking for a format-only alternate's format does not resolve");

  streams[1].startable = false;
  const EspUsbHostAudioStreamSelection none =
      espUsbHostSelectAudioStreamForFormat(streams.data(), streams.size(), false, 0, 0, 0);
  check(!none, "a device whose alternates are all format-only resolves nothing");
}

void testSelectContinuousRange()
{
  EspUsbHostAudioStreamInfo stream = pcmStream(false, 2, 16, {});
  stream.sampleRateMin = 8000;
  stream.sampleRateMax = 96000;
  stream.sampleRate = 8000;

  const EspUsbHostAudioStreamSelection best =
      espUsbHostSelectAudioStreamForFormat(&stream, 1, false, 0, 0, 0);
  check(static_cast<bool>(best), "a continuous range resolves");
  checkEqual(best.sampleRate, 48000, "48 kHz is picked out of a continuous range");

  const EspUsbHostAudioStreamSelection pinned =
      espUsbHostSelectAudioStreamForFormat(&stream, 1, false, 0, 0, 96000);
  checkEqual(pinned.sampleRate, 96000, "a rate inside the range can be pinned");

  const EspUsbHostAudioStreamSelection outside =
      espUsbHostSelectAudioStreamForFormat(&stream, 1, false, 0, 0, 192000);
  check(!outside, "a rate outside the range does not resolve");
}

// Explicit feedback payloads, USB 2.0 section 5.12.4.2. At full speed the device
// reports samples per 1 ms frame in 10.14 format (3 bytes); 48 kHz is 48 samples
// per frame, so the raw value is 48 << 14 = 0x0C0000.
void testDecodeFeedbackFullSpeed()
{
  const uint8_t nominal[3] = {0x00, 0x00, 0x0c};
  const uint32_t q16 = espUsbHostAudioDecodeFeedbackQ16(nominal, sizeof(nominal));
  checkEqual(q16, 48u << 16, "a 10.14 payload is normalised to 16.16");
  checkEqual(espUsbHostAudioFeedbackSampleRate(q16, false), 48000,
             "48 samples per frame is 48000 Hz at full speed");

  // 47.5 samples per frame: the device is asking the host to slow down.
  const uint8_t slower[3] = {0x00, 0xe0, 0x0b};
  checkEqual(espUsbHostAudioFeedbackSampleRate(
                 espUsbHostAudioDecodeFeedbackQ16(slower, sizeof(slower)), false),
             47500,
             "a fractional 10.14 value keeps its fraction");

  // 44.1 kHz is 44.1 samples per frame, which 10.14 cannot hold exactly.
  const uint32_t raw441 = static_cast<uint32_t>(44.1 * 16384.0);
  const uint8_t f441[3] = {static_cast<uint8_t>(raw441 & 0xff),
                           static_cast<uint8_t>((raw441 >> 8) & 0xff),
                           static_cast<uint8_t>((raw441 >> 16) & 0xff)};
  const uint32_t rate441 = espUsbHostAudioFeedbackSampleRate(
      espUsbHostAudioDecodeFeedbackQ16(f441, sizeof(f441)), false);
  check(rate441 >= 44099 && rate441 <= 44101, "44.1 kHz decodes within a hertz");
}

// A 4-byte payload is already 16.16, and at high speed the value counts samples
// per 125 us microframe: 48 kHz is 6 samples per microframe.
void testDecodeFeedbackHighSpeed()
{
  const uint8_t nominal[4] = {0x00, 0x00, 0x06, 0x00};
  const uint32_t q16 = espUsbHostAudioDecodeFeedbackQ16(nominal, sizeof(nominal));
  checkEqual(q16, 6u << 16, "a 4-byte payload is taken as 16.16 unchanged");
  checkEqual(espUsbHostAudioFeedbackSampleRate(q16, true), 48000,
             "6 samples per microframe is 48000 Hz at high speed");
  checkEqual(espUsbHostAudioFeedbackSampleRate(q16, false), 6000,
             "the same value read as full speed is samples per millisecond");
}

void testDecodeFeedbackRejects()
{
  const uint8_t payload[4] = {0x00, 0x00, 0x0c, 0x00};
  checkEqual(espUsbHostAudioDecodeFeedbackQ16(nullptr, 3), 0, "a null payload decodes to 0");
  checkEqual(espUsbHostAudioDecodeFeedbackQ16(payload, 0), 0, "an empty packet decodes to 0");
  checkEqual(espUsbHostAudioDecodeFeedbackQ16(payload, 2), 0, "a 2-byte packet decodes to 0");
  checkEqual(espUsbHostAudioFeedbackSampleRate(0, false), 0, "a zero value is a zero rate");
}

// The +/-12.5% window that guards the pacing rate.
void testFeedbackPlausibility()
{
  check(espUsbHostAudioFeedbackRatePlausible(48000, 48000), "the nominal rate is plausible");
  check(espUsbHostAudioFeedbackRatePlausible(48100, 48000), "a small drift is plausible");
  check(espUsbHostAudioFeedbackRatePlausible(47000, 48000), "a small drift down is plausible");
  check(espUsbHostAudioFeedbackRatePlausible(48000 + 48000 / 8, 48000),
        "the upper bound is inclusive");
  check(espUsbHostAudioFeedbackRatePlausible(48000 - 48000 / 8, 48000),
        "the lower bound is inclusive");
  check(!espUsbHostAudioFeedbackRatePlausible(48001 + 48000 / 8, 48000),
        "a rate above the window is rejected");
  check(!espUsbHostAudioFeedbackRatePlausible(24000, 48000),
        "half the nominal rate is rejected");
  check(!espUsbHostAudioFeedbackRatePlausible(96000, 48000),
        "twice the nominal rate is rejected");
  check(!espUsbHostAudioFeedbackRatePlausible(0, 48000), "a zero rate is rejected");
  check(!espUsbHostAudioFeedbackRatePlausible(48000, 0), "a zero nominal rate rejects everything");
}

void testSelectEmpty()
{
  check(!espUsbHostSelectAudioStreamForFormat(nullptr, 0, false, 0, 0, 0),
        "a null stream array resolves nothing");
  const EspUsbHostAudioStreamInfo stream = pcmStream(true, 1, 16, {48000});
  check(!espUsbHostSelectAudioStreamForFormat(&stream, 1, false, 0, 0, 0),
        "an input-only device resolves no output stream");
}

} // namespace

int main()
{
  testFeatureUnitLayoutUac1();
  testFeatureUnitLayoutUac2();
  testFeatureUnitLayoutRejects();
  testControlMasks();
  testFeedbackEndpoint();
  testDecodeFeedbackFullSpeed();
  testDecodeFeedbackHighSpeed();
  testDecodeFeedbackRejects();
  testFeedbackPlausibility();
  testRangeSubRangeCount();
  testDecodeSampleRateRange();
  testDecodeVolumeRange();
  testReadU32();
  testSelectExactFormat();
  testSelectWildcards();
  testSelectSkipsFormatOnlyStreams();
  testSelectContinuousRange();
  testSelectEmpty();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all USB Audio decoding and selection checks passed\n");
  return 0;
}
