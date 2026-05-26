def test_usb_msc_capacity(dut, peers):
    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_usb_msc_capacity64(dut, peers):
    dut.write("C")
    dut.expect_exact("MSC_CAPACITY64 ok=1 blocks=16 block_size=512")


def test_usb_msc_inquiry(dut, peers):
    dut.write("i")
    dut.expect_exact("MSC_INQUIRY ok=1 removable=1 vendor='ESP32' product='MSC_PEER' revision='1.0'")


def test_usb_msc_request_sense(dut, peers):
    dut.write("s")
    dut.expect_exact("MSC_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")
    dut.write("S")
    dut.expect_exact("MSC_LAST_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")


def test_usb_msc_test_unit_ready(dut, peers):
    dut.write("t")
    dut.expect_exact("MSC_TEST_UNIT_READY ok=1")


def test_usb_msc_read_boot_block(dut, peers):
    dut.write("r")
    dut.expect_exact("MSC_READ ok=1 b0=eb b1=3c b510=55 b511=aa")


def test_usb_msc_write_read_block(dut, peers):
    device = peers["device"]

    dut.write("w")
    device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")


def test_usb_msc_multi_block_write_read(dut, peers):
    device = peers["device"]

    dut.write("m")
    device.expect_exact("DEVICE_WRITE lba=6 offset=0 size=512")
    device.expect_exact("DEVICE_WRITE lba=7 offset=0 size=512")
    dut.expect_exact("MSC_MULTI write=1 read=1 b0=31 b511=30 b512=31 b1023=30")


def test_usb_msc_chunked_write_read(dut, peers):
    device = peers["device"]

    dut.write("g")
    for lba in range(1, 10):
        device.expect_exact(f"DEVICE_WRITE lba={lba} offset=0 size=512")
    dut.expect_exact("MSC_CHUNKED write=1 read=1 b0=17 b4095=14 b4096=17 b4607=14")


def test_usb_msc_out_of_range_is_rejected(dut, peers):
    dut.write("o")
    dut.expect_exact("MSC_OUT_OF_RANGE read=0 write=0")
