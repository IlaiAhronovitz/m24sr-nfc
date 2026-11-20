#!/usr/bin/env python3
"""build.py - build, test, flash, run and restore m24sr-nfc. No make needed.

    python build.py [build]   build/m24sr-nfc.elf (+ .bin, .map)
    python build.py test      host tests (tests/, no board), with the host gcc
    python build.py flash     build, then program the board and reset it
    python build.py run       flash, wait, then print the report block
    python build.py read      print the report block again
    python build.py restore   put back the factory demo (backup/, SHA-256 checked)

The board is an ST M24SR-DISCOVERY (STM32F103RG) behind an ST-LINK/V2 on its
20-pin JTAG header. Several ST-LINK/V2 (PID 3748) exist on this PC: pass
--serial when more than one is plugged in.
Environment: GCC_BIN (arm-none-eabi folder), HOSTCC (host C compiler), OPENOCD.
"""
import argparse
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import report  # noqa: E402

DEFAULT_GCC = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
OCD_GLOB = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages",
                        "xpack-dev-tools.openocd-xpack_*", "xpack-openocd-*")
ELF = "build/m24sr-nfc.elf"
BACKUP = "backup/factory_flash_2026-11-20.bin"
BACKUP_SHA256 = "816F84F0130E450F6A8C27F59847BCF72966777149A87F0B84098820BC0BFBBC"
REPORT_ADDR = 0x20000000

ARCH = ["-mcpu=cortex-m3", "-mthumb"]
CFLAGS = ARCH + ["-Os", "-g3", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-ffreestanding", "-ffunction-sections", "-fdata-sections", "-Isrc"]
LDFLAGS = ARCH + ["-nostartfiles", "-nostdlib", "-Tlinker/stm32f103rg.ld",
                  "-Wl,--gc-sections", "-Wl,-Map=build/m24sr-nfc.map"]
PORTABLE = ["src/crc_a.c", "src/ndef.c", "src/m24sr.c"]


def tool(name):
    gcc_bin = os.environ.get("GCC_BIN", DEFAULT_GCC)
    path = os.path.join(gcc_bin, name + (".exe" if os.name == "nt" else ""))
    if os.path.exists(path):
        return path
    found = shutil.which(name)
    if not found:
        sys.exit(f"{name} not found: set GCC_BIN")
    return found


def run(cmd, **kw):
    print(" ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def build(write_uri=None):
    os.makedirs("build", exist_ok=True)
    cc = tool("arm-none-eabi-gcc")
    defs = []
    if write_uri:
        if '"' in write_uri or "\\" in write_uri:
            sys.exit("--write-uri must not contain quotes or backslashes")
        defs = [f'-DWRITE_URI="{write_uri}"']
    objs = []
    for src in sorted(glob.glob("src/*.c")) + ["src/startup.s"]:
        obj = os.path.join("build", os.path.basename(src) + ".o")
        run([cc] + CFLAGS + defs + ["-c", src, "-o", obj])
        objs.append(obj)
    run([cc] + LDFLAGS + objs + ["-lgcc", "-o", ELF])
    run([tool("arm-none-eabi-objcopy"), "-O", "binary", ELF, "build/m24sr-nfc.bin"])
    run([tool("arm-none-eabi-size"), ELF])


def test():
    os.makedirs("build", exist_ok=True)
    cc = os.environ.get("HOSTCC", "gcc")
    exe = os.path.join("build", "host_tests" + (".exe" if os.name == "nt" else ""))
    run([cc, "-std=c99", "-O1", "-Wall", "-Wextra", "-Werror", "-o", exe,
         "tests/test_main.c", "tests/mock_m24sr.c"] + PORTABLE)
    return subprocess.call([exe])


def openocd(args, serial, capture=True):
    exe = os.environ.get("OPENOCD")
    if exe:
        scripts = os.path.join(os.path.dirname(os.path.dirname(exe)), "openocd", "scripts")
    else:
        roots = glob.glob(OCD_GLOB)
        if not roots:
            sys.exit("OpenOCD not found: set OPENOCD")
        exe = os.path.join(roots[0], "bin", "openocd.exe")
        scripts = os.path.join(roots[0], "openocd", "scripts")
    cmd = [exe, "-s", scripts, "-f", "interface/stlink.cfg"]
    if serial:
        cmd += ["-c", f"adapter serial {serial}"]
    cmd += ["-f", "target/stm32f1x.cfg"] + args
    return run(cmd, capture_output=capture, text=True)


def dump(serial):
    path = "build/report.bin"
    # Halt first: with the core asleep in WFI, this ST-LINK/V2 reads zeros.
    openocd(["-c", "init", "-c", "halt",
             "-c", f"dump_image {path} 0x{REPORT_ADDR:08X} {report.SIZE}",
             "-c", "resume", "-c", "shutdown"], serial)
    with open(path, "rb") as f:
        return f.read()


def restore(serial):
    with open(BACKUP, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest().upper()
    if digest != BACKUP_SHA256:
        sys.exit(f"backup checksum mismatch: {digest}")
    openocd(["-c", "init", "-c", "reset halt",
             "-c", f"flash write_image erase {BACKUP} 0x08000000 bin",
             "-c", f"verify_image {BACKUP} 0x08000000 bin",
             "-c", "reset run", "-c", "shutdown"], serial, capture=False)


def main():
    os.chdir(ROOT)
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", nargs="?", default="build",
                    choices=["build", "test", "flash", "run", "read", "restore"])
    ap.add_argument("--serial")
    ap.add_argument("--write-uri", help="build in a step that writes this URI to the tag")
    a = ap.parse_args()

    if a.cmd == "build":
        build(a.write_uri)
    elif a.cmd == "test":
        sys.exit(test())
    elif a.cmd == "restore":
        restore(a.serial)
    elif a.cmd == "flash":
        build(a.write_uri)
        openocd(["-c", f"program {ELF} verify reset exit"], a.serial)
    else:
        if a.cmd == "run":
            build(a.write_uri)
            openocd(["-c", f"program {ELF} verify reset exit"], a.serial)
            time.sleep(2)
        print(report.to_text(report.parse(dump(a.serial))))


if __name__ == "__main__":
    main()
