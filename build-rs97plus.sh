#!/usr/bin/env bash
set -euo pipefail

KERNEL_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN_BIN="/home/sean/retrofw-dev/kernel-tools/opt/mipseltools-gcc412-glibc261/bin"
HELPER_BIN="/home/sean/retrofw-dev/bin"

export PATH="${TOOLCHAIN_BIN}:${HELPER_BIN}:${PATH}"
export ARCH=mips
export CROSS_COMPILE=mipsel-linux-

cd "$KERNEL_DIR"

if [ "${1:-}" = "--clean" ] || [ ! -f .config ]; then
    echo "=== Configuring RetroFW Kernel+ for RS-97 Plus (Y030XX067A / RETROGAME) ==="
    make RetroFW_defconfig LCD=Y030XX067A GPIO=RETROGAME

    # RS-97 Plus Kernel+ tunings (Phase 4):
    # 1. Native noop flash I/O scheduler
    ./scripts/config --disable DEFAULT_CFQ --enable DEFAULT_NOOP --set-str DEFAULT_IOSCHED "noop"
    # 2. Tighter 250 Hz timer ticks + voluntary preemption for smoother 60fps & audio
    ./scripts/config --disable HZ_100 --enable HZ_250
    ./scripts/config --disable PREEMPT_NONE --enable PREEMPT_VOLUNTARY
    # 3. Disable SLUB allocator debug overhead to reclaim RAM & cycles
    ./scripts/config --disable SLUB_DEBUG
    # 4. Built-in LZO compressed RAM swap (/dev/ramzswap0 + /proc/ramzswap)
    ./scripts/config --enable BLK_DEV_JZ_RAMZSWAP --enable LZO_COMPRESS --enable LZO_DECOMPRESS
    # 5. Larger SquashFS fragment cache (8 vs 3) for snappier rootfs & OPK loading
    ./scripts/config --set-val SQUASHFS_FRAGMENT_CACHE_SIZE 8

    set +o pipefail
    yes "" | make oldconfig >/dev/null
    set -o pipefail
fi

echo "=== Active Kernel+ Config ==="
grep -E 'CONFIG_LCD_Y030XX067A|CONFIG_GPIO_RETROGAME|CONFIG_HZ=|CONFIG_PREEMPT_VOLUNTARY|CONFIG_SLUB_DEBUG|CONFIG_DEFAULT_IOSCHED|CONFIG_BLK_DEV_JZ_RAMZSWAP|CONFIG_LZO_|CONFIG_SQUASHFS_FRAGMENT_CACHE_SIZE' .config || true

echo "=== Building uImage + modules with $(mipsel-linux-gcc --version | head -n1) ==="
make -j"$(nproc)" uImage modules

echo "=== Installing matching .ko modules into modules-out/ ==="
rm -rf "$KERNEL_DIR/modules-out"
make INSTALL_MOD_PATH="$KERNEL_DIR/modules-out" INSTALL_MOD_STRIP=1 modules_install

# Generate clean relative modules.dep matching RetroFW 2.3's busybox modprobe
MODDIR="$KERNEL_DIR/modules-out/lib/modules/2.6.31.3"
cat << 'EOF' > "$MODDIR/modules.dep"
kernel/drivers/usb/gadget/g_ether.ko:
kernel/drivers/usb/gadget/g_file_storage.ko:
kernel/drivers/video/console/fbcon.ko: kernel/drivers/video/console/bitblit.ko kernel/drivers/video/console/softcursor.ko kernel/drivers/video/console/font.ko
kernel/drivers/video/console/bitblit.ko: kernel/drivers/video/console/softcursor.ko
kernel/drivers/video/console/font.ko:
kernel/drivers/video/console/softcursor.ko:
EOF

echo "=== Build complete ==="
ls -lh arch/mips/boot/uImage
sha256sum arch/mips/boot/uImage
find "$MODDIR" -name '*.ko' -exec ls -lh {} +
