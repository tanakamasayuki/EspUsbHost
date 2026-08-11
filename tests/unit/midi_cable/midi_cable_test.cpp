// Host tests for the USB MIDI Streaming cable-count decoder.
//
// This file is compiled and run on the host with g++ (no board required) by
// test_midi_cable.py, which extracts the helper under test verbatim from
// src/EspUsbHost.h into "espusbhost_midi_real.h". The checks therefore run
// against the production decoder, not a copy.
//
// Descriptor bytes below follow the USB Device Class Definition for MIDI Devices
// 1.0, and the multi-cable samples match what the sibling EspUsbDevice library
// emits, so the peer test and this test describe the same layouts.

#include "espusbhost_midi_real.h"

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

// CS_ENDPOINT / MS_GENERAL: bLength, bDescriptorType, bDescriptorSubtype,
// bNumEmbMIDIJack, baAssocJackID[].
std::vector<uint8_t> msGeneral(const std::vector<uint8_t> &jackIds)
{
  std::vector<uint8_t> descriptor = {
      static_cast<uint8_t>(4 + jackIds.size()),
      ESP_USB_HOST_MIDI_CS_ENDPOINT,
      ESP_USB_HOST_MIDI_MS_GENERAL,
      static_cast<uint8_t>(jackIds.size()),
  };
  descriptor.insert(descriptor.end(), jackIds.begin(), jackIds.end());
  return descriptor;
}

// ---------------------------------------------------------------------------
// Well-formed descriptors
// ---------------------------------------------------------------------------

void testSingleCable()
{
  // The common one-port keyboard: a single embedded jack on each endpoint.
  const std::vector<uint8_t> descriptor = msGeneral({0x01});
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 1,
             "one embedded jack is one cable");
}

void testMultipleCables()
{
  // A four-port interface. Jack IDs need not be contiguous or ordered; the cable
  // number is the index into the array, not the jack ID.
  const std::vector<uint8_t> descriptor = msGeneral({0x01, 0x05, 0x03, 0x09});
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 4,
             "cable count comes from bNumEmbMIDIJack, not from the jack IDs");
}

void testMaximumCables()
{
  // A cable number is 4 bits wide, so 16 is the largest legal count.
  std::vector<uint8_t> jackIds;
  for (uint8_t i = 0; i < ESP_USB_HOST_MIDI_MAX_CABLES; i++)
  {
    jackIds.push_back(static_cast<uint8_t>(i + 1));
  }
  const std::vector<uint8_t> descriptor = msGeneral(jackIds);
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()),
             ESP_USB_HOST_MIDI_MAX_CABLES,
             "16 cables is accepted");
}

// ---------------------------------------------------------------------------
// Descriptors that must be rejected
// ---------------------------------------------------------------------------

void testRejectsNull()
{
  checkEqual(espUsbHostMidiEndpointCableCount(nullptr), 0, "null data is rejected");
}

void testRejectsZeroCables()
{
  // A bulk endpoint with no embedded jacks carries nothing addressable.
  const std::vector<uint8_t> descriptor = msGeneral({});
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "bNumEmbMIDIJack = 0 yields no cables");
}

void testRejectsWrongDescriptorType()
{
  // CS_INTERFACE (0x24) rather than CS_ENDPOINT (0x25). The MS_HEADER at the top
  // of a MIDI Streaming interface has this shape and must not be mistaken for an
  // endpoint descriptor.
  std::vector<uint8_t> descriptor = msGeneral({0x01, 0x02});
  descriptor[1] = 0x24;
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "a CS_INTERFACE descriptor is rejected");
}

void testRejectsWrongSubtype()
{
  // A class-specific endpoint descriptor of some other subtype, such as the
  // audio EP_GENERAL that sits after an isochronous endpoint.
  std::vector<uint8_t> descriptor = msGeneral({0x01, 0x02});
  descriptor[2] = 0x02;
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "a non-MS_GENERAL subtype is rejected");
}

void testRejectsTruncatedJackArray()
{
  // bNumEmbMIDIJack claims four jacks but bLength only covers two.
  std::vector<uint8_t> descriptor = msGeneral({0x01, 0x02});
  descriptor[3] = 4;
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "a jack array shorter than bNumEmbMIDIJack is rejected");
}

void testRejectsShortHeader()
{
  // Too short to hold bNumEmbMIDIJack at all. The helper must not read past
  // bLength to discover that.
  const std::vector<uint8_t> descriptor = {
      3, ESP_USB_HOST_MIDI_CS_ENDPOINT, ESP_USB_HOST_MIDI_MS_GENERAL};
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "a descriptor shorter than 4 bytes is rejected");
}

void testRejectsMoreThanSixteenCables()
{
  // 17 embedded jacks: the descriptor is self-consistent but a cable number
  // cannot address the last one, so the device is describing something the class
  // cannot express.
  std::vector<uint8_t> jackIds;
  for (uint8_t i = 0; i < ESP_USB_HOST_MIDI_MAX_CABLES + 1; i++)
  {
    jackIds.push_back(static_cast<uint8_t>(i + 1));
  }
  const std::vector<uint8_t> descriptor = msGeneral(jackIds);
  checkEqual(espUsbHostMidiEndpointCableCount(descriptor.data()), 0,
             "more cables than a cable number can address is rejected");
}

// ---------------------------------------------------------------------------
// The reported structure
// ---------------------------------------------------------------------------

void testPortInfoDefaults()
{
  // getMidiPortInfo() leaves these at zero for a device whose descriptors carry
  // no MS_GENERAL, so a caller can treat zero as "no cables in this direction".
  const EspUsbHostMidiPortInfo info;
  checkEqual(info.address, 0, "default address is zero");
  checkEqual(info.interfaceNumber, 0, "default interface number is zero");
  checkEqual(info.inCableCount, 0, "default IN cable count is zero");
  checkEqual(info.outCableCount, 0, "default OUT cable count is zero");
}

void testDirectionsAreIndependent()
{
  // A device may be asymmetric: the class puts a separate MS_GENERAL on each
  // bulk endpoint, so the two directions are decoded from different descriptors
  // and need not agree. A 2-in / 1-out interface is a real configuration.
  const std::vector<uint8_t> in = msGeneral({0x01, 0x02});
  const std::vector<uint8_t> out = msGeneral({0x03});
  EspUsbHostMidiPortInfo info;
  info.inCableCount = espUsbHostMidiEndpointCableCount(in.data());
  info.outCableCount = espUsbHostMidiEndpointCableCount(out.data());
  checkEqual(info.inCableCount, 2, "IN direction takes its count from its own endpoint");
  checkEqual(info.outCableCount, 1, "OUT direction takes its count from its own endpoint");
  check(info.inCableCount != info.outCableCount, "the two directions may differ");
}

}  // namespace

int main()
{
  testSingleCable();
  testMultipleCables();
  testMaximumCables();
  testRejectsNull();
  testRejectsZeroCables();
  testRejectsWrongDescriptorType();
  testRejectsWrongSubtype();
  testRejectsTruncatedJackArray();
  testRejectsShortHeader();
  testRejectsMoreThanSixteenCables();
  testPortInfoDefaults();
  testDirectionsAreIndependent();

  if (failures != 0)
  {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all MIDI cable checks passed\n");
  return 0;
}
