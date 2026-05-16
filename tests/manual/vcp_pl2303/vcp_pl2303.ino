#include "VcpLoopbackTest.h"

VcpLoopbackTest vcpTest(0x067b, 0x2303);

void setup() { vcpTest.setup(); }
void loop() { vcpTest.loop(); }
