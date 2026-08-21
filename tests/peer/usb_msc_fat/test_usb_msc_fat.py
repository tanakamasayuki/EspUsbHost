def test_usb_msc_fat_peer_volume_is_formatted(dut, peers):
    """The peer formats its RAM disk with FatFs before USB comes up."""
    device = peers["device"]
    device.write("s")
    device.expect_exact("DEVICE_FAT_STATUS formatted=1 blocks=256 block_size=512")


def test_usb_msc_fat_mount_read_unmount(dut, peers):
    dut.write("m")
    dut.expect_exact("FAT_MOUNT ok=1 mounted=1 error=ESP_OK")
    dut.write("r")
    dut.expect_exact("FAT_READ ok=1 data=MSC_FAT_PEER")
    dut.write("u")
    dut.expect_exact("FAT_UNMOUNT ok=1 mounted=0 error=ESP_OK")


def test_usb_msc_fat_unmount_then_end_then_rebegin(dut, peers):
    """The reported sequence: mount, unmount, end(), begin(), mount again."""
    dut.write("c")
    dut.expect_exact("CYCLE_MOUNT ok=1")
    dut.expect_exact("CYCLE_READ ok=1 data=MSC_FAT_PEER")
    dut.expect_exact("CYCLE_UNMOUNT ok=1 error=ESP_OK")
    dut.expect_exact("CYCLE_STOP mounted=0 error=ESP_OK")
    dut.expect_exact("CYCLE_RESTART ready=1 error=ESP_OK", timeout=20)
    dut.expect_exact("CYCLE_REMOUNT ok=1", timeout=20)
    dut.expect_exact("CYCLE_REREAD ok=1 data=MSC_FAT_PEER")
    dut.expect_exact("CYCLE_FINAL_UNMOUNT ok=1")


def test_usb_msc_fat_end_releases_a_still_mounted_volume(dut, peers):
    """end() must drop the mounts it owns, not just the USB side.

    A mount holds a FatFs drive slot and a registered VFS path, and the mount
    table has FF_VOLUMES (2) entries. Leaving one behind makes the next
    mscMount() of the same basePath fail with "basePath already mounted", and
    the drive slots run out after two cycles.
    """
    dut.write("k")
    dut.expect_exact("KEEP_MOUNT ok=1")
    dut.expect_exact("KEEP_STOP mounted=0 error=ESP_OK")
    dut.expect_exact("KEEP_RESTART ready=1 error=ESP_OK", timeout=20)
    dut.expect_exact("KEEP_REMOUNT ok=1", timeout=20)
    dut.expect_exact("KEEP_REREAD ok=1 data=MSC_FAT_PEER")
    dut.expect_exact("KEEP_FINAL_UNMOUNT ok=1")
