def test_usb_msc_capacity(dut, peers):
    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_usb_msc_block_device_info(dut, peers):
    dut.write("d")
    dut.expect("MSC_BLOCK_DEVICE ok=1 addr=\\d+ iface=\\d+ lun=0 max_lun=0 blocks=16 block_size=512 bytes=8192")


def test_usb_msc_capacity64(dut, peers):
    dut.write("C")
    dut.expect_exact("MSC_CAPACITY64 ok=1 blocks=16 block_size=512")


def test_usb_msc_inquiry(dut, peers):
    dut.write("i")
    dut.expect_exact("MSC_INQUIRY ok=1 removable=1 vendor='ESP32' product='MSC_PEER' revision='1.0'")


def test_usb_msc_max_lun(dut, peers):
    dut.write("l")
    dut.expect_exact("MSC_MAX_LUN ok=1 max_lun=0")


def test_usb_msc_select_lun(dut, peers):
    dut.write("L")
    dut.expect_exact("MSC_SELECT_LUN ok=1")


def test_usb_msc_request_sense(dut, peers):
    dut.write("s")
    dut.expect_exact("MSC_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")
    dut.write("S")
    dut.expect_exact("MSC_LAST_SENSE ok=1 response=0x70 key=0x00 asc=0x00 ascq=0x00")


def test_usb_msc_test_unit_ready(dut, peers):
    dut.write("t")
    dut.expect_exact("MSC_TEST_UNIT_READY ok=1")


def test_usb_msc_wait_ready(dut, peers):
    dut.write("T")
    dut.expect_exact("MSC_WAIT_READY ok=1")


def test_usb_msc_synchronize_cache(dut, peers):
    dut.write("y")
    dut.expect_exact("MSC_SYNC_CACHE ok=1")


def test_usb_msc_read_boot_block(dut, peers):
    dut.write("r")
    dut.expect_exact("MSC_READ ok=1 b0=eb b1=3c b510=55 b511=aa")


def test_usb_msc_read64_boot_block(dut, peers):
    dut.write("R")
    dut.expect_exact("MSC_READ64 ok=1 b0=eb b1=3c b510=55 b511=aa")


def test_usb_msc_write_read_block(dut, peers):
    device = peers["device"]

    dut.write("w")
    device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")


def test_usb_msc_write_read64_block(dut, peers):
    device = peers["device"]

    dut.write("W")
    device.expect_exact("DEVICE_WRITE lba=5 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ64 write=1 read=1 b0=5a b1=5b b255=a5 b511=a5")


def test_usb_msc_multi_block_write_read(dut, peers):
    device = peers["device"]

    dut.write("m")
    device.expect_exact("DEVICE_WRITE lba=6 offset=0 size=512")
    device.expect_exact("DEVICE_WRITE lba=7 offset=0 size=512")
    dut.expect_exact("MSC_MULTI write=1 read=1 b0=31 b511=30 b512=31 b1023=30")


def test_usb_msc_chunked_write_read(dut, peers):
    dut.write("g")
    dut.expect_exact("MSC_CHUNKED write=1 read=1 b0=17 b4095=14 b4096=17 b4607=14")


def test_usb_msc_out_of_range_is_rejected(dut, peers):
    dut.write("o")
    dut.expect_exact("MSC_OUT_OF_RANGE read=0 write=0")


def test_usb_msc_failed_write_is_reported(dut, peers):
    device = peers["device"]

    device.write("F")
    device.expect_exact("DEVICE_FAIL_NEXT_WRITE armed=1")
    dut.write("e")
    device.expect_exact("DEVICE_WRITE_FAIL lba=10 offset=0 size=512")
    dut.expect_exact("MSC_FAILED_WRITE write=0")

    # Reset recovery must leave the bulk endpoints usable for the next command.
    dut.write("w")
    device.expect_exact("DEVICE_WRITE lba=4 offset=0 size=512")
    dut.expect_exact("MSC_WRITE_READ write=1 read=1 b0=a5 b1=a4 b255=5a b511=5a")


def test_usb_msc_end_rebegin_with_device_attached(dut, peers):
    """end() uninstalls the host library while the MSC device is attached."""
    dut.write("X")
    dut.expect_exact("HOST_STOP installed=0 clients=-1 devices=-1 ready=0")
    dut.expect_exact("HOST_RESTART ready=1 error=ESP_OK")

    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")


def test_usb_msc_end_rebegin_with_empty_device_list(dut, peers):
    """end() must uninstall the host library with nothing left on the bus.

    Reported as issue #42: begin() after end() fails with ESP_ERR_INVALID_STATE.
    usb_host_device_free_all() returns ESP_OK for an empty device list, so the
    shutdown handles no library event and the USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS
    flag left behind by the client deregister makes usb_host_uninstall() refuse.
    The host library stays installed, so every later begin() fails in
    usb_host_install(). A device attached during teardown hides this, because
    then the ALL_FREE wait handles the events that clear the flag.
    """
    device = peers["device"]

    dut.write("x")
    dut.expect_exact("HOST_RESTART_ARMED 1")

    # Rebooting the peer is the only way this setup can empty the bus; the
    # device core has no USB detach API. The DUT runs the cycle on its own as
    # soon as the disconnect arrives, so the ~500 ms the peer needs to come
    # back is not spent waiting for a serial round trip.
    device.write("z")
    device.expect_exact("DEVICE_REBOOT")

    dut.expect_exact("DEVICE_DISCONNECTED", timeout=20)
    dut.expect_exact("HOST_IDLE_STOP devices_before=0 installed=0 error=ESP_OK")
    dut.expect_exact("HOST_IDLE_RESTART ready=1 error=ESP_OK")

    # The peer must enumerate again on the restarted host and still work.
    dut.expect_exact("DEVICE_CONNECTED", timeout=30)
    dut.write("c")
    dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")
