#include "VcpLoopbackTest.h"

VcpLoopbackTest vcpTest(0x0403);

void setup() { vcpTest.setup(); }
void loop() { vcpTest.loop(); }
