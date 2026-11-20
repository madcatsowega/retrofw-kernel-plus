# RetroFW Kernel+ (RS-97 Plus / JZ4760B)

Custom Linux `2.6.31.3` kernel workspace for the **RS-97 Plus** (`JZ4760B`, `128 MB RAM`, `Y030XX067A` 320×480 IPS panel, `GPIO=RETROGAME`), compiled with the official RetroFW `gcc 4.1.2` toolchain so all stock RetroFW 2.3 `rootfs.squashfs` modules (`g_ether.ko`, `g_file_storage.ko`, `fbcon.ko`) load with 100% ABI compatibility.

## Patches Applied

1. **In-tree `uImage` build fix** (`arch/mips/boot/Makefile`):
   - Use `$(srctree)/$(obj)/tools/entry` instead of `$(KBUILD_SRC)/$(obj)/tools/entry` so in-tree `make uImage` resolves the kernel entry point cleanly.
2. **USB Gadget Hotplug fix for RS-97 Plus** (`drivers/usb/musb/vbus_hotplug.c`):
   - On RS-97 Plus boards where `PD7` (GPIO 103) reads `0` even when a USB cable is connected, stock `vbus_hotplug` stayed permanently `offline` and never called `musb_start()`.
   - `uh_register_notifier()` now schedules a non-blocking USB SOF probe (`DO_FORCE_DETECT`) in `kuhd` whenever a gadget driver (`g_ether` / `g_file_storage`) loads and `cable_state != UH_CABLE_STATE_USB`.
   - `/sys/devices/platform/musb_hdrc.0/uh_notify` now supports:
     - `auto` / `detect`: probe USB bus SOF directly (ignoring `PD7` if unwired) without dropping an already-active session.
     - `usb`: force `UH_CABLE_STATE_USB` and start MUSB immediately.
     - `offline`: stop MUSB and suspend the OTG PHY.
   - Zero extra idle battery drain when no USB gadget is loaded.

## Build

```bash
./build-rs97plus.sh
```

Produces `arch/mips/boot/uImage` (`Load: 0x80010000`, `Entry: 0x80015840`, ~2.1 MB).

## Safe Backup, Flash & Rollback

`flash-kernel-sd.sh` ONLY touches the 4 MiB kernel slot at offset `0x400000` (never U-Boot at `0x000000` or partitions at `0x800000+`), automatically backs up the stock kernel into `backups/`, and verifies the written bytes via SHA-256:

```bash
# Flash (with automatic stock kernel backup)
sudo ./flash-kernel-sd.sh /dev/sdX

# Instant rollback to stock kernel
sudo ./flash-kernel-sd.sh --restore /dev/sdX backups/uImage-stock-YYYYMMDD-HHMMSS.bin
```
