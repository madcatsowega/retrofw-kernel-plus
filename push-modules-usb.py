#!/usr/bin/env python3
"""Push Kernel+ matching .ko modules and autoexec.sh hooks to RS-97 Plus over USB FTP/Telnet."""
import ftplib
import os
import sys

sys.path.insert(0, "/home/sean/gmenunx-plus/scripts")

HOST = "169.254.1.1"
LOCAL_MOD_ROOT = "/home/sean/retrofw-kernel-plus/modules-out/lib/modules/2.6.31.3"
REMOTE_MOD_ROOT = "/home/retrofw/apps/gmenu2x-plus/modules/2.6.31.3"

def ensure_remote_dir(ftp: ftplib.FTP, path: str) -> None:
    parts = [p for p in path.split("/") if p]
    cur = ""
    for p in parts:
        cur += "/" + p
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass

def main() -> int:
    if not os.path.isdir(LOCAL_MOD_ROOT):
        print(f"ERROR: {LOCAL_MOD_ROOT} not found. Run ./build-rs97plus.sh first.", file=sys.stderr)
        return 1

    ftp = ftplib.FTP()
    ftp.connect(HOST, 21, timeout=5)
    ftp.login("root", "")

    count = 0
    for root, _, files in os.walk(LOCAL_MOD_ROOT):
        rel_dir = os.path.relpath(root, LOCAL_MOD_ROOT)
        rem_dir = REMOTE_MOD_ROOT if rel_dir == "." else f"{REMOTE_MOD_ROOT}/{rel_dir}"
        ensure_remote_dir(ftp, rem_dir)
        for fn in sorted(files):
            if not (fn.endswith(".ko") or fn == "modules.dep"):
                continue
            local_p = os.path.join(root, fn)
            remote_p = f"{rem_dir}/{fn}"
            with open(local_p, "rb") as f:
                ftp.storbinary(f"STOR {remote_p}", f)
            print(f"  -> {remote_p} ({os.path.getsize(local_p)} B)")
            count += 1

    ftp.quit()
    print(f"Uploaded {count} module files to {REMOTE_MOD_ROOT}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
