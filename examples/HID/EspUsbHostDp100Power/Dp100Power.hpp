// Meaning layer for the ALIENTEK DP100: raw frame fields turned into volts, amps
// and degrees.
//
// Dp100Device.hpp moves frames; this decides what they mean. The read side is
// confirmed against hardware (tests/probe/dp100, tests/manual/dp100). The write
// side is not - see setVoltage() and friends.

#pragma once

#include "Dp100Device.hpp"

namespace dp100
{

// Model limits, used to refuse a setting the supply would clip anyway. The DP100
// is a 30 V / 5 A buck-boost supply, but the ceiling that matters at any moment is
// what the present input allows, which BASIC_INFO reports as vo_max.
static constexpr float MAX_VOLTAGE = 30.0f;
static constexpr float MAX_CURRENT = 5.0f;

// One DP100 as a power supply rather than as a frame pump.
class Dp100Power
{
public:
  explicit Dp100Power(EspUsbHost &host) : device_(host) {}

  bool begin() { return device_.begin(); }
  void end() { device_.end(); }
  bool ready() const { return device_.ready(); }
  Dp100Device &raw() { return device_; }

  bool identify(DeviceInfo &info) { return device_.readDeviceInfo(info); }

  // One BASIC_INFO read, kept so the accessors below can be asked afterwards
  // without another round trip.
  bool refresh()
  {
    if (!device_.readBasicInfo(basic_))
    {
      return false;
    }
    valid_ = true;
    return true;
  }

  const BasicInfo &basic() const { return basic_; }
  bool valid() const { return valid_; }

  float inputVoltage() const { return basic_.inputMillivolts / 1000.0f; }
  float outputVoltage() const { return basic_.outputMillivolts / 1000.0f; }
  float outputCurrent() const { return basic_.outputMilliamps / 1000.0f; }
  // The highest output the present input supports, which is the real ceiling for
  // a setpoint - a 12 V input cannot make 20 V out.
  float maxOutputVoltage() const { return basic_.maxOutputMillivolts / 1000.0f; }
  float temperature1() const { return basic_.temperature1Deci / 10.0f; }
  float temperature2() const { return basic_.temperature2Deci / 10.0f; }
  float rail5v() const { return basic_.rail5vMillivolts / 1000.0f; }
  uint8_t outputMode() const { return basic_.outputMode; }
  uint8_t workStatus() const { return basic_.workStatus; }

  // ---------------------------------------------------------------------------
  // Write side. Confirmed on hardware (tests/probe/dp100, tests/manual/dp100_output):
  // a write goes to the group index with the write flag added, and the state byte is
  // the output enable - state 1 put 5.000 V on the terminals, state 0 took it back
  // to 0. A write to a bare index is answered with success and then ignored, which
  // is why the flag lives in the encoder rather than in a caller's hands.
  //
  // These change what a bench supply is doing. Know what is connected to the output
  // terminals before calling them.
  // ---------------------------------------------------------------------------

  // Reads the setpoint the supply runs from (index 0), or one of its stored presets.
  bool readSetpoint(BasicSet &set, uint8_t index = 0) { return device_.readBasicSet(set, index); }

  // Applies voltage, current and the output state in one frame - the only form the
  // device offers, so a caller that wants to change one of them reads the setpoint
  // first. The protection thresholds are carried through unchanged.
  bool apply(float volts, float amps, bool outputOn, uint8_t index = 0)
  {
    if (volts < 0.0f || volts > MAX_VOLTAGE || amps < 0.0f || amps > MAX_CURRENT)
    {
      return false;
    }
    BasicSet set;
    if (!device_.readBasicSet(set, index))
    {
      return false;
    }
    set.index = index;
    set.state = outputOn ? STATE_OUTPUT_ON : STATE_OUTPUT_OFF;
    set.voltageMillivolts = static_cast<uint16_t>(volts * 1000.0f + 0.5f);
    set.currentMilliamps = static_cast<uint16_t>(amps * 1000.0f + 0.5f);
    return device_.writeBasicSet(set);
  }

  // Changes the setpoint, leaving the output where it is.
  bool setVoltage(float volts, uint8_t index = 0)
  {
    BasicSet set;
    if (!device_.readBasicSet(set, index) || volts < 0.0f || volts > MAX_VOLTAGE)
    {
      return false;
    }
    set.index = index;
    set.voltageMillivolts = static_cast<uint16_t>(volts * 1000.0f + 0.5f);
    return device_.writeBasicSet(set);
  }

  bool setCurrent(float amps, uint8_t index = 0)
  {
    BasicSet set;
    if (!device_.readBasicSet(set, index) || amps < 0.0f || amps > MAX_CURRENT)
    {
      return false;
    }
    set.index = index;
    set.currentMilliamps = static_cast<uint16_t>(amps * 1000.0f + 0.5f);
    return device_.writeBasicSet(set);
  }

  // Switches the output, keeping the present setpoint.
  bool setOutput(bool on, uint8_t index = 0)
  {
    BasicSet set;
    if (!device_.readBasicSet(set, index))
    {
      return false;
    }
    set.index = index;
    set.state = on ? STATE_OUTPUT_ON : STATE_OUTPUT_OFF;
    return device_.writeBasicSet(set);
  }

  Dp100Device &device() { return device_; }

private:
  Dp100Device device_;
  BasicInfo basic_;
  bool valid_ = false;
};

} // namespace dp100
