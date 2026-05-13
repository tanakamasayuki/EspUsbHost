// Smoke test sketch for manual test procedure verification.
// Prints "SMOKE ready" once on boot. No EspUsbHost or USB hardware required.

void setup()
{
    Serial.begin(115200);
    delay(5000);
    Serial.println("SMOKE ready");
}

void loop()
{
    delay(1);
}
