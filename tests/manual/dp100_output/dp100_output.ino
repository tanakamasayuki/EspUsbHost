// Write-path test for an ALIENTEK DP100 (ATK-MDP100, 2e3c:af01): change the
// setpoint, switch the output on, confirm the voltage actually appears, switch it
// off again and put the original setpoint back.
//
// THIS ENERGISES THE SUPPLY'S OUTPUT TERMINALS at 5.000 V / 0.500 A. Run it only
// with nothing connected to them. The read-only checks live in tests/manual/dp100.
//
// The point of the test is that a write is not taken on trust: the DP100 answers a
// write it then ignores with the same success status as one it applies (a write
// whose index lacks the 0x20 flag does exactly that), so every step here is
// confirmed by reading the setpoint back and by reading what the output is doing.
//
// The protocol, device and model layers live with the example so there is one
// source of truth; tests/ is stripped from release archives while examples/ is
// not, so the dependency only ever points this way.
#include "../../../examples/HID/EspUsbHostDp100Power/Dp100Power.hpp"

static constexpr uint32_t TEST_TIMEOUT_MS = 30000;
static constexpr uint32_t SETTLE_MS = 1500;

// Deliberately low, and inside what a 12 V input can produce.
static constexpr float TEST_VOLTAGE = 5.0f;
static constexpr float TEST_CURRENT = 0.5f;
// How long the output is left on, and how close the measured voltage must land.
static constexpr uint32_t OUTPUT_SETTLE_MS = 600;
static constexpr float VOLTAGE_TOLERANCE = 0.3f;

static EspUsbHost usb;
static dp100::Dp100Power supply(usb);
static bool finished = false;
static uint32_t lastDeviceEventMs = 0;

static bool nearly(float actual, float expected)
{
  const float difference = actual > expected ? actual - expected : expected - actual;
  return difference <= VOLTAGE_TOLERANCE;
}

// Reads what the output is doing right now, after giving the supply a moment.
static bool measure(float &volts, float &amps)
{
  delay(OUTPUT_SETTLE_MS);
  if (!supply.refresh())
  {
    return false;
  }
  volts = supply.outputVoltage();
  amps = supply.outputCurrent();
  return true;
}

static bool runChecks()
{
  bool ok = true;

  if (!supply.begin())
  {
    Serial.println("begin failed");
    return false;
  }

  // The setpoint to put back at the end, whatever happens in between.
  dp100::BasicSet original;
  if (!supply.readSetpoint(original))
  {
    Serial.println("reading the original setpoint failed");
    return false;
  }
  Serial.printf("original setpoint index=%u state=0x%02x %umV %umA ovp=%umV ocp=%umA\n",
                original.index,
                original.state,
                original.voltageMillivolts,
                original.currentMilliamps,
                original.overVoltageMillivolts,
                original.overCurrentMilliamps);

  float volts = 0.0f;
  float amps = 0.0f;
  if (!measure(volts, amps))
  {
    Serial.println("BASIC_INFO failed");
    return false;
  }
  Serial.printf("before: out %.3fV %.3fA\n", volts, amps);
  if (volts > 0.5f)
  {
    Serial.println("the output is already on; run this with the output off");
    return false;
  }

  // 1. Setpoint only, output left off. This separates "the write frame is right"
  //    from "the output enable is right".
  if (!supply.apply(TEST_VOLTAGE, TEST_CURRENT, false))
  {
    Serial.println("setpoint write failed");
    ok = false;
  }
  dp100::BasicSet readback;
  if (!supply.readSetpoint(readback))
  {
    Serial.println("setpoint readback failed");
    ok = false;
  }
  else
  {
    Serial.printf("after write: setpoint %umV %umA state=0x%02x\n",
                  readback.voltageMillivolts, readback.currentMilliamps, readback.state);
    if (readback.voltageMillivolts != 5000 || readback.currentMilliamps != 500)
    {
      Serial.println("the setpoint did not take, so the write flag or field order is wrong");
      ok = false;
    }
    if (readback.state != dp100::STATE_OUTPUT_OFF)
    {
      Serial.println("the output state changed when it should not have");
      ok = false;
    }
    if (readback.overVoltageMillivolts != original.overVoltageMillivolts ||
        readback.overCurrentMilliamps != original.overCurrentMilliamps)
    {
      Serial.println("the protection thresholds were not carried through");
      ok = false;
    }
  }
  if (measure(volts, amps))
  {
    Serial.printf("after setpoint write: out %.3fV %.3fA\n", volts, amps);
    if (volts > 0.5f)
    {
      Serial.println("the output came on from a setpoint write alone");
      ok = false;
    }
  }

  // 2. Output on. This is the step that only hardware can confirm: the answer to
  //    the write says nothing about whether a voltage appeared.
  if (!supply.setOutput(true))
  {
    Serial.println("output on failed");
    ok = false;
  }
  else if (!measure(volts, amps))
  {
    Serial.println("BASIC_INFO failed with the output on");
    ok = false;
  }
  else
  {
    Serial.printf("output on: out %.3fV %.3fA mode=%u status=%u\n",
                  volts, amps, supply.outputMode(), supply.workStatus());
    if (!nearly(volts, TEST_VOLTAGE))
    {
      Serial.printf("the output did not reach %.3fV\n", TEST_VOLTAGE);
      ok = false;
    }
  }

  // 3. Output off again, confirmed the same way.
  if (!supply.setOutput(false))
  {
    Serial.println("output off failed");
    ok = false;
  }
  else if (!measure(volts, amps))
  {
    Serial.println("BASIC_INFO failed after switching off");
    ok = false;
  }
  else
  {
    Serial.printf("output off: out %.3fV %.3fA\n", volts, amps);
    if (volts > 0.5f)
    {
      Serial.println("the output did not switch off");
      ok = false;
    }
  }

  // 4. Put the supply back the way it was found, and prove it took.
  if (!supply.device().writeBasicSet(original))
  {
    Serial.println("restoring the original setpoint failed");
    ok = false;
  }
  else if (!supply.readSetpoint(readback))
  {
    Serial.println("reading back the restored setpoint failed");
    ok = false;
  }
  else
  {
    Serial.printf("restored: %umV %umA state=0x%02x\n",
                  readback.voltageMillivolts, readback.currentMilliamps, readback.state);
    if (readback.voltageMillivolts != original.voltageMillivolts ||
        readback.currentMilliamps != original.currentMilliamps)
    {
      Serial.println("the original setpoint was not restored");
      ok = false;
    }
  }
  if (measure(volts, amps))
  {
    Serial.printf("final: out %.3fV %.3fA\n", volts, amps);
    if (volts > 0.5f)
    {
      Serial.println("the output is still on at the end of the test");
      ok = false;
    }
  }

  Serial.printf("refusals=%lu received=%lu\n",
                static_cast<unsigned long>(supply.device().refusalCount()),
                static_cast<unsigned long>(supply.device().receivedCount()));
  if (supply.device().refusalCount() != 0)
  {
    Serial.println("the device refused at least one frame");
    ok = false;
  }

  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
        lastDeviceEventMs = millis();
        Serial.printf("connected address=%u vid=%04x pid=%04x product=\"%s\"\n",
                      device.address, device.vid, device.pid, device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
        lastDeviceEventMs = millis();
        Serial.printf("disconnected address=%u\n", device.address); });

  usb.begin();
  lastDeviceEventMs = millis();
  Serial.println("dp100_output test start");
  Serial.println("Connect an ALIENTEK DP100 with NOTHING on its output terminals.");
}

void loop()
{
  static uint32_t startedAt = millis();

  if (!finished)
  {
    if (usb.deviceCount() > 0 && millis() - lastDeviceEventMs >= SETTLE_MS)
    {
      const bool ok = runChecks();
      Serial.println(ok ? "[PASS]" : "[FAIL]");
      finished = true;
    }
    else if (millis() - startedAt > TEST_TIMEOUT_MS)
    {
      usb.printAllDeviceInfo();
      Serial.println("[FAIL]");
      Serial.println("No DP100 was enumerated.");
      finished = true;
    }
  }

  delay(10);
}
