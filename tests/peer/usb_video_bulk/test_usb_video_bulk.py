"""Bulk UVC must receive multi-packet payloads and survive stop/restart."""

import time


def test_bulk_uvc_frames_and_restart(dut, peers):
    device = peers["device"]
    # The same gating used by the isochronous camera keeps peer flashing out of
    # host enumeration. Query until the actual camera, not ROM USB, is present.
    dut.write("G")
    try:
        for _ in range(60):
            dut.write("v")
            state = dut.expect(r"VIDEO_DEVICE addr=\d+ streams=(\d+)\r?\n", timeout=3)
            if int(state.group(1)):
                break
            time.sleep(0.2)
        else:
            raise AssertionError("bulk camera did not enumerate")

        device.write("v")
        device.expect(r"DEVICE_VIDEO .* bulk=1\r?\n")
        dut.write("d")
        dut.expect(r"VIDEO_STREAM .* bulk .* startable=1\r?\n")

        for _ in range(5):
            dut.write("S")
            start = dut.expect(r"VIDEO_START started=(\d).* payload=(\d+) error=(\S+)\r?\n")
            assert start.group(1) == b"1", start.group(3).decode()
            assert int(start.group(2)) >= 128, "payload must span multiple 64-byte USB packets"
            time.sleep(1)
            dut.write("F")
            frame = dut.expect(
                r"VIDEO_FRAMES have=1 streaming=1 seen=(\d+) good=(\d+) incomplete=(\d+) "
                r"last_len=(\d+) mismatch=(\d) at=\d+\r?\n")
            device.write("q")
            sent = device.expect(r"DEVICE_VIDEO_STATE streaming=(\d) sent=(\d+)\r?\n")
            print(f"bulk frames: received={int(frame.group(1))}, good={int(frame.group(2))}, "
                  f"peer streaming={int(sent.group(1))}, sent={int(sent.group(2))}")
            assert int(frame.group(2)) >= 3, "bulk stream delivered no complete patterned frames"
            assert int(frame.group(3)) == 0
            assert int(frame.group(4)) == 8192
            assert int(frame.group(5)) == 0
            stats = dut.expect(
                r"VIDEO_STATS frames=\d+ incomplete=\d+ payloads=(\d+) header_errors=(\d+) "
                r"payload_errors=(\d+) packet_errors=(\d+) overflows=(\d+) bytes=\d+\r?\n")
            assert int(stats.group(1)) > 0
            assert all(int(stats.group(i)) == 0 for i in range(2, 6))
            assert start.group(3) == b"ESP_OK"
            dut.write("T")
            dut.expect_exact("VIDEO_STOP stopped=1 streaming=0")
    finally:
        dut.write("H")
        dut.expect_exact("HOST_STATE idle devices=0")
