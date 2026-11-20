# RetroFW Kernel+ (RS-97 Plus / JZ4760B)

Custom Linux `2.6.31.3` kernel workspace for the **RS-97 Plus** (`JZ4760B`, `128 MB RAM`, `Y030XX067A` 320×480 IPS panel, `GPIO=RETROGAME`), compiled with RetroFW's official `gcc 4.1.2` toolchain.

## Patches & Upgrades Included

1. **In-tree `uImage` build fix** (`arch/mips/boot/Makefile`):
   - Uses `$(srctree)/$(obj)/tools/entry` so in-tree `make uImage` resolves the kernel entry point cleanly.
2. **USB Gadget Hotplug & PHY Start Fix** (`drivers/usb/musb/vbus_hotplug.c`, `drivers/usb/musb/musb_core.c`):
   - Enables the USB OTG PHY clock *before* asserting `MUSB_POWER_SOFTCONN` in `musb_start()`.
   - Unconditionally brings `uh_cable` online when a gadget driver (`g_ether` / `g_file_storage`) loads on boards where `PD7` reads `0` (unlocking high-speed USB Ethernet `169.254.1.1` FTP/Telnet on RS-97 Plus with zero extra idle battery drain).
3. **Built-in LZO Compressed RAM Swap** (`drivers/block/jz_ramzswap.c`):
   - Exposes `/dev/ramzswap0` (**48 MiB** virtual swap, zero bytes allocated until used) backed by kernel `LZO1X` compression + zero-page deduplication, with live stats at `/proc/ramzswap`.
   - Activated at priority `100` ahead of SD card swap (`mmcblk0p2`) via `mkswap /dev/ramzswap0 && swapon -p 100 /dev/ramzswap0`.
4. **Safe Kernel-Side CPU Clock Controller** (`arch/mips/jz4760b/proc.c` → `/proc/jz/clock`):
   - `cat /proc/jz/clock` reports current `CCLK`, `HCLK`, `MCLK`, `REFCNT`, and `PIXCLK`.
   - `echo <mhz> > /proc/jz/clock` (`240..720 MHz`, 6 MHz step) atomically scales `CPPCR0` with IRQs disabled, updates `REG_DDRC_REFCNT` proportionally to `MCLK`, and dynamically adjusts `LPCDR` so the `Y030XX067A` display stays locked at **~60 Hz** (`35.2 MHz`) across all CPU speeds.
5. **Scheduler, Preemption & Flash I/O Tuning** (`build-rs97plus.sh`):
   - `CONFIG_DEFAULT_IOSCHED="noop"`
   - `CONFIG_HZ_250=y` (4 ms ticks vs stock 10 ms)
   - `CONFIG_PREEMPT_VOLUNTARY=y`
   - `# CONFIG_SLUB_DEBUG is not set`
   - Builds the 6 matching `.ko` modules (`g_ether.ko`, `g_file_storage.ko`, `fbcon.ko`, `bitblit.ko`, `font.ko`, `softcursor.ko`) into `modules-out/lib/modules/2.6.31.3/`.

## Build

```bash
./build-rs97plus.sh --clean
```

Produces:
- `arch/mips/boot/uImage` (`Load: 0x80010000`, `Entry: 0x80015840`, ~2.1 MB)
- `modules-out/lib/modules/2.6.31.3/` (matching `.ko` modules + `modules.dep`)

## Safe Backup, Flash & Rollback

`flash-kernel-sd.sh` ONLY touches the 4 MiB kernel slot at offset `0x400000` (never U-Boot at `0x000000` or partitions at `0x800000+`), automatically backs up the existing kernel into `backups/`, and verifies written bytes via SHA-256:

```bash
# Flash (with automatic kernel backup)
sudo ./flash-kernel-sd.sh /dev/sdX

# Instant rollback to original stock Y030XX067A kernel
sudo ./flash-kernel-sd.sh --restore /dev/sdX backups/uImage-stock-20261119-222549.bin
```
