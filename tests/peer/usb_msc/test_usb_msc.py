def test_usb_msc_capacity(dut, peers):
    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_usb_msc_read_boot_block(dut, peers):
    dut.write("r")
    dut.expect_exact("MSC_READ ok=1 b0=eb b1=3c b510=55 b511=aa")


def test_usb_msc_write_read_block(dut, peers):
    device = peers["device"]

    dut.write("w")
    device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")
