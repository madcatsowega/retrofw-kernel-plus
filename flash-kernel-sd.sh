#!/usr/bin/env bash
set -euo pipefail

# Safe RS-97 Plus kernel flasher / restorer for RetroFW SD cards.
# Only touches the 4 MiB uImage slot at offset 0x400000 (4194304 bytes).
# NEVER touches U-Boot (0x000000..0x07e000) or partitions (0x800000+).

usage() {
    echo "Usage:"
    echo "  sudo $0 <block-dev> [uImage-path]          # Backup stock kernel + flash new uImage"
    echo "  sudo $0 --restore <block-dev> <backup-bin> # Restore backed-up stock kernel"
    exit 1
}

RESTORE=0
if [ "${1:-}" = "--restore" ]; then
    RESTORE=1
    shift
fi

DEV="${1:-}"
[ -n "$DEV" ] || usage
[ -b "$DEV" ] || { echo "ERROR: $DEV is not a block device" >&2; exit 1; }

KERNEL_DIR="$(cd "$(dirname "$0")" && pwd)"
IMG="${2:-$KERNEL_DIR/arch/mips/boot/uImage}"
[ -f "$IMG" ] || { echo "ERROR: image not found: $IMG" >&2; exit 1; }

# Safety check 1: must be removable USB/SD device, never root disk
BASE="$(basename "$DEV")"
RM="$(cat "/sys/block/$BASE/removable" 2>/dev/null || echo 0)"
if [ "$RM" != "1" ]; then
    echo "ERROR: $DEV is not marked removable (removable=$RM). Refusing to write!" >&2
    exit 1
fi

# Safety check 2: ensure no partitions on $DEV are mounted
if lsblk -no MOUNTPOINT "$DEV" | grep -q '[^[:space:]]'; then
    echo "ERROR: One or more partitions on $DEV are mounted. Unmount them first!" >&2
    lsblk -o NAME,SIZE,LABEL,MOUNTPOINT "$DEV" >&2
    exit 1
fi

# Safety check 3: verify uImage magic (0x27051956) and size <= 4 MiB
SIZE=$(stat -c%s "$IMG")
if [ "$SIZE" -lt 1024 ] || [ "$SIZE" -gt 4194304 ]; then
    echo "ERROR: $IMG size ($SIZE bytes) is outside valid range (1 KiB .. 4 MiB)" >&2
    exit 1
fi
MAGIC=$(od -An -tx1 -N4 "$IMG" | tr -d ' \n')
if [ "$MAGIC" != "27051956" ]; then
    echo "ERROR: $IMG does not start with uImage magic 27051956 (got: $MAGIC)" >&2
    exit 1
fi

# Safety check 4: verify existing SD card slot at 0x400000 has uImage magic
SD_MAGIC=$(dd if="$DEV" bs=1 skip=4194304 count=4 status=none | od -An -tx1 | tr -d ' \n')
if [ "$SD_MAGIC" != "27051956" ]; then
    echo "ERROR: $DEV offset 0x400000 does not contain a RetroFW uImage (got: $SD_MAGIC). Refusing!" >&2
    exit 1
fi

if [ "$RESTORE" -eq 0 ]; then
    mkdir -p "$KERNEL_DIR/backups"
    BACKUP="$KERNEL_DIR/backups/uImage-stock-$(date +%Y%m%d-%H%M%S).bin"
    echo "=== Backing up existing 4 MiB kernel slot to $BACKUP ==="
    dd if="$DEV" of="$BACKUP" bs=1M skip=4 count=4 status=progress
    sync
    echo "Backup saved: $BACKUP ($(sha256sum "$BACKUP" | awk '{print $1}'))"
fi

echo "=== Writing $IMG ($SIZE bytes) to $DEV at offset 0x400000 ==="
dd if="$IMG" of="$DEV" bs=1M seek=4 conv=notrunc status=progress
sync

echo "=== Verifying written bytes ==="
EXP_SHA=$(sha256sum "$IMG" | awk '{print $1}')
GOT_SHA=$(dd if="$DEV" bs=1 skip=4194304 count="$SIZE" status=none | sha256sum | awk '{print $1}')
if [ "$EXP_SHA" != "$GOT_SHA" ]; then
    echo "ERROR: Read-back verification FAILED! ($EXP_SHA != $GOT_SHA)" >&2
    exit 1
fi
echo "VERIFIED OK: $GOT_SHA"
sync
