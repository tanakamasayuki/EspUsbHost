// SCPI layer for a KIKUSUI PMX-series DC power supply, on top of UsbtmcDevice.
//
// This is the instrument-specific file, and the only one to replace when
// pointing the example at a different USBTMC instrument: UsbtmcProtocol.hpp and
// UsbtmcDevice.hpp know nothing about SCPI or about this VID/PID.
//
// The commands used here are IEEE 488.2 common commands and standard SCPI power
// supply nodes, so most of them work unchanged on other programmable supplies.
// See README.md for the manual this is checked against, and for the trademark
// notice.

#pragma once

#include "UsbtmcDevice.hpp"

#include <stdlib.h>

namespace pmx
{

// KIKUSUI PMX18-5A. Other PMX models share the command set and differ only in
// their limits, so the PID is not pinned by default.
static constexpr uint16_t VID = 0x0b3e;
static constexpr uint16_t PID_PMX18_5A = 0x1029;

// Model limits, used to refuse a setting the supply would clip anyway.
static constexpr float MAX_VOLTAGE = 18.0f;
static constexpr float MAX_CURRENT = 5.0f;

class ScpiPmx
{
public:
  explicit ScpiPmx(EspUsbHost &host) : device_(host) {}

  // Claims the instrument and clears its status. Leaves the output as it is:
  // switching it is the sketch's decision, not this layer's.
  bool begin(uint16_t pid = 0)
  {
    if (!device_.begin(VID, pid))
    {
      return false;
    }
    return device_.write("*CLS");
  }

  void end() { device_.end(); }
  bool ready() const { return device_.ready(); }
  usbtmc::UsbtmcDevice &usbtmc() { return device_; }

  // "KIKUSUI,PMX18-5A,<serial>,<firmware>"
  bool identify(char *response, size_t responseSize)
  {
    return device_.query("*IDN?", response, responseSize);
  }

  bool setVoltage(float volts)
  {
    if (volts < 0.0f || volts > MAX_VOLTAGE)
    {
      return false;
    }
    return writeFloat("VOLT", volts);
  }

  bool setCurrent(float amps)
  {
    if (amps < 0.0f || amps > MAX_CURRENT)
    {
      return false;
    }
    return writeFloat("CURR", amps);
  }

  // Configured values, which are what the supply will produce - not what it is
  // producing now. Use measure*() for that.
  bool voltageSetting(float &volts) { return queryFloat("VOLT?", volts); }
  bool currentSetting(float &amps) { return queryFloat("CURR?", amps); }

  bool setOutput(bool on) { return device_.write(on ? "OUTP ON" : "OUTP OFF"); }

  bool output(bool &on)
  {
    char response[32];
    if (!device_.query("OUTP?", response, sizeof(response)))
    {
      return false;
    }
    // The supply answers 1/0; accept ON/OFF as well so the same code works on
    // instruments that spell it out.
    on = response[0] == '1' || strncmp(response, "ON", 2) == 0;
    return true;
  }

  bool measureVoltage(float &volts) { return queryFloat("MEAS:VOLT?", volts); }
  bool measureCurrent(float &amps) { return queryFloat("MEAS:CURR?", amps); }

  // SCPI error queue. Returns the code and leaves the message in text; 0 means
  // the queue was empty, which is the check that every command so far was
  // accepted.
  bool error(int &code, char *text, size_t textSize)
  {
    char response[96];
    if (!device_.query("SYST:ERR?", response, sizeof(response)))
    {
      return false;
    }
    code = atoi(response);
    if (text && textSize > 0)
    {
      const char *comma = strchr(response, ',');
      const char *message = comma ? comma + 1 : response;
      strncpy(text, message, textSize - 1);
      text[textSize - 1] = '\0';
    }
    return true;
  }

  // Blocks until the instrument has finished the commands sent so far. USB488
  // can signal completion over the interrupt IN endpoint, which this example
  // does not open, so *OPC? is the portable way to wait.
  bool waitForOperationComplete()
  {
    char response[16];
    return device_.query("*OPC?", response, sizeof(response)) && response[0] == '1';
  }

private:
  bool writeFloat(const char *node, float value)
  {
    char command[32];
    snprintf(command, sizeof(command), "%s %.3f", node, value);
    return device_.write(command);
  }

  bool queryFloat(const char *command, float &value)
  {
    char response[32];
    if (!device_.query(command, response, sizeof(response)))
    {
      return false;
    }
    value = strtof(response, nullptr);
    return true;
  }

  usbtmc::UsbtmcDevice device_;
};

} // namespace pmx
