#include <DemoAdd.h>

void setup()
{
  Serial.begin(115200);
  delay(1000);

  DemoAdd demo;
  int result = demo.add(1, 2);

  Serial.print("ADD_RESULT ");
  Serial.println(result);
}

void loop()
{
}
