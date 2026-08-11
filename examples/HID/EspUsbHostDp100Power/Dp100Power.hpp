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
  // Write side. UNVERIFIED.
  //
  // BASIC_SET carries the setpoints and the output enable in one frame, so
  // probing it blind can switch a bench supply's output on. tests/probe/dp100
  // therefore stops short of it and tests/manual/dp100 never calls it. The frame
  // layout below is what the public reverse-engineering projects describe; it is
  // not confirmed by this project's own measurement.
  //
  // Before using these on your own bench: disconnect whatever is on the output
  // terminals, keep the values low, and check the front panel after each call.
  // ---------------------------------------------------------------------------

  // Applies a full setpoint. The other calls are conveniences over this one.
  bool apply(float volts, float amps, bool outputOn, uint8_t index = 0)
  {
    if (volts < 0.0f || volts > MAX_VOLTAGE || amps < 0.0f || amps > MAX_CURRENT)
    {
      return false;
    }
    BasicSet set;
    set.index = index;
    set.state = outputOn ? SET_STATE_ACTIVATE : SET_STATE_ACTIVE;
    set.voltageMillivolts = static_cast<uint16_t>(volts * 1000.0f + 0.5f);
    set.currentMilliamps = static_cast<uint16_t>(amps * 1000.0f + 0.5f);
    // Protection thresholds a little above the setpoint, so a setpoint change does
    // not trip the supply on its own.
    set.overVoltageMillivolts = static_cast<uint16_t>(set.voltageMillivolts + 500);
    set.overCurrentMilliamps = static_cast<uint16_t>(set.currentMilliamps + 100);

    Response response;
    if (!device_.writeBasicSet(set, response))
    {
      return false;
    }
    return !isRefusal(response);
  }

  Dp100Device &device() { return device_; }

private:
  Dp100Device device_;
  BasicInfo basic_;
  bool valid_ = false;
};

} // namespace dp100
