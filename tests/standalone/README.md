# Standalone Tests

`tests/standalone` contains tests that run on one ESP32-S3 host board.
The intended hardware is an ESP32-S3 with a 4-port USB hub connected so tests can
use up to four downstream USB devices.

Arduino CLI profile:

- `s3_hub_host`: ESP32-S3 USB host board with the 4-port hub

Set the matching serial port in `.env`:

```sh
TEST_SERIAL_PORT_S3_HUB_HOST=/dev/ttyACM1
```
