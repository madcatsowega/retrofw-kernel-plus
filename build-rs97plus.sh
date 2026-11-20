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
    echo "=== Configuring RetroFW kernel for RS-97 Plus (Y030XX067A / RETROGAME) ==="
    make RetroFW_defconfig LCD=Y030XX067A GPIO=RETROGAME
fi

echo "=== Building uImage with $(mipsel-linux-gcc --version | head -n1) ==="
make -j"$(nproc)" uImage

echo "=== Build complete ==="
ls -lh arch/mips/boot/uImage
sha256sum arch/mips/boot/uImage
