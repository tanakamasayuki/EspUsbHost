#include "VcpLoopbackTest.h"

VcpLoopbackTest vcpTest(0x10c4);

void setup() { vcpTest.setup(); }
void loop() { vcpTest.loop(); }
