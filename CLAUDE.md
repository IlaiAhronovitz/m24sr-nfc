# m24sr-nfc: agent rules

An NFC Forum Type 4 Tag stack in bare-metal C for ST's dynamic NFC tag M24SR (M24SR64-Y), driven over I2C from an
STM32F103. Register level, no HAL, no vendor library. Layers: `src/i2c.c` (I2C2 master, polled), `src/m24sr.c` +
`src/crc_a.c` (framing, ISO 7816-4 commands, I2C/RF session token), `src/ndef.c` (NDEF records, URI and Text).
`src/lc/` is a portable library (an AT-response parser and a "Last Checkpoint" record for LTE-M/NB-IoT devices): it
is host-tested, compiled for Cortex-M and checked by the quality gate like the firmware, but **not linked into the
firmware image yet**. `README.md` has the layers and the first board run; `tests/lc/README.md` documents `src/lc/`.

If a `CLAUDE.local.md` exists next to this file, read it too: it holds the notes for one machine and is git-ignored.

## Board
- ST **M24SR-DISCOVERY (MB1138-B)**: STM32F103RG (Cortex-M3, 1 MiB flash, 96 KiB RAM), M24SR64-Y on **I2C2
  (PB10 SCL, PB11 SDA)**, TFT on SPI2 (PB12 to PB15, unused), joystick, LEDs.
- ST-LINK/V2 on the 20-pin JTAG header (USB 0483:3748). ST-LINK/V2 probes share that USB ID: **pass
  `--serial <SN>`** whenever more than one is plugged in. No virtual COM port: the firmware writes a status block and a
  text log to RAM at `0x20000000` (4 KiB), and `build.py` reads it over SWD.
- Firmware stays on **HSI 8 MHz** (no crystal, no PLL); I2C2 runs at 100 kHz. The factory firmware ran at 72 MHz.
- The factory image is dumped into `backup/` before the first flash; `build.py` records its SHA-256 and `restore`
  checks it.

## Commands (run in the repo root)
```
python build.py                          # build/m24sr-nfc.elf (+ .bin, .map)
python build.py test                     # host tests: CRC_A vectors, NDEF codec, M24SR flow against a mock tag, lc
python build.py quality                  # the quality gate (Linux, or WSL on Windows: QUALITY_WSL_DISTRO)
python build.py quality --only misra     # one check; `bash tools/quality.sh --list` names them
python build.py run --serial <SN>        # build, flash, print the report from the board
python build.py run --write-uri "https://example.com/x"   # write one URI record, read it back, verify
python build.py read --serial <SN>       # print the report again
python build.py restore --serial <SN>    # factory demo back
```
Tools: Arm GNU Toolchain 14.2 (`GCC_BIN`), xPack OpenOCD 0.12 (`OPENOCD`), host gcc (`HOSTCC`), Python 3.
Never flash or reset a board you were not given. A passing run prints `I2C errors 0` and `verify: ok`.

## Quality gate (mandatory before any commit)
`python build.py quality` must print `RESULT: PASS`: MISRA C:2012 (cppcheck), CERT C and clang-tidy, strict gcc and
clang warnings as errors, ASan and UBSan on host tests and the AT fuzzer, coverage, stack frames under 1 KiB. A
SKIPPED check fails the run. Only a local run may accept one, by naming it (`--allow-skip target-gcc`) and then
running that check another way (`tools/quality/README.md`); CI never does. No blanket suppressions, no `#pragma` to
silence a tool: a finding is fixed or covered by a record in `docs/deviations/` (`common.md`, `target.md`) with a
matching line in `tools/quality/suppress-*.txt`. Read `docs/CODING-STANDARD.md` first. The linker asserts at least
8 KiB of stack headroom.

## Owner's rule: research first, no architecture change without an approved ADR
**No change to code or memory architecture** (driver structure, the I2C/framing/NDEF layering, the report-block and
RAM layout, persistent formats written to the tag, linker layout) **without a sourced ADR, marked PROPOSED and
approved by the owner.** Allowed without an ADR: bug fixes, standard-compliance fixes (MISRA, CERT, ISO 7816-4,
ISO 14443-3 CRC_A, NFC Forum T4T), measurement tooling, tests, docs.
Protocol claims cite a source (datasheet section, RM0008 section) or are marked as estimates.

## Board and tag safety
- **Never touch option bytes or RDP** on the F103, and do not change the BOOT0 setting.
- The factory image in `backup/` is ST's: **never commit it**, never delete it. `restore` verifies its checksum.
- NDEF writes set NLEN to 0 first and to the real length last: keep that order, so a phone never reads half a message.
- The M24SR gives the I2C or the RF side the token, not both: end a session with DESELECT, and expect a GetI2Csession
  refusal if an RF session is open (the firmware then kills the RF session). Do not set the I2C password (planned work).
- The GPO and RF-disable lines of this board are not found yet; do not drive guessed pins.
- I2C bus recovery (9 clocks and a STOP) is part of init: do not remove it.

## Public-repo hygiene
No personal data (names, e-mail, user-name paths, serial numbers of personal gear, details of one machine).
`research/`, `backup/`, `build/` and `CLAUDE.local.md` are git-ignored: **never add them, or anything from them, to a
commit**, and do not link to them from tracked files. Stage files by name, not with `git add -A`.
