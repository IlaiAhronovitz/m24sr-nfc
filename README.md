# m24sr-nfc

An NFC Forum Type 4 Tag stack in bare-metal C for ST's dynamic NFC tag, the
M24SR, driven over I2C from an STM32F103. It is register level, with no HAL
and no vendor library. The protocol layers are tested on the host against a
mock tag.

## Layers

| Layer | File | What it does |
|---|---|---|
| I2C master | `src/i2c.c` | STM32F1 I2C2, polled. It follows the reception sequences of RM0008 for 1, 2 and more bytes, guards the ACK/STOP windows against interrupts, and recovers a stuck bus with 9 clocks and a STOP. |
| Framing | `src/m24sr.c`, `src/crc_a.c` | I-blocks with a toggling block number, S-blocks for DESELECT and WTX, CRC_A (ISO/IEC 14443-3), and polling while the tag NACKs its address. |
| Commands | `src/m24sr.c` | ISO 7816-4: Select application and file, ReadBinary, UpdateBinary. GetI2Csession and KillRFsession for the I2C/RF token. Capability container parsing. |
| NDEF | `src/ndef.c` | Record parser (short and long records, ID field). URI records with the 36 RTD URI prefixes, and UTF-8 Text records, both built and decoded. |
| Last Checkpoint | `src/lc/` | A portable library, not linked into this firmware yet: a bounded AT-response parser (3GPP TS 27.007) and a fixed-size diagnostic record for LTE-M/NB-IoT devices, with a text summary and a CRC-checked binary form. See `tests/lc/README.md`. |

When writing a message, NLEN is set to 0 first and to the real length last, so a
phone never reads half a message.

## Board

ST M24SR-DISCOVERY (MB1138-B): an STM32F103RG (1 MiB flash, 96 KiB RAM) with an
M24SR64-Y, a TFT, a joystick and LEDs. The debugger is an ST-LINK/V2 on the
20-pin JTAG header.

The board's wiring was read from its factory firmware over SWD, because the
schematic is not in a public document:
- the M24SR is on I2C2 (PB10 SCL, PB11 SDA);
- the TFT is on SPI2 (PB12 to PB15);
- the factory firmware runs at 72 MHz.

This firmware stays on HSI at 8 MHz, so it does not depend on the crystal.

The factory demo was saved before the first flash, with its SHA-256 recorded in `build.py`;
`python build.py restore` puts it back. The image itself is ST's and stays out of this repo:
dump your own board first.

An ST-LINK/V2 has no virtual COM port. So the firmware writes a status block
and a text log to RAM at 0x2000_0000, and `build.py` reads it over SWD.

## First run on the board (2026-11-20)

```
I2C session: ok
select NDEF application: ok
read CC: ok
  CC: 00 0F 20 00 F6 00 F6 04 06 00 01 20 00 00 00
  mapping version 20, NDEF file 0001, max 8192 bytes, MLe 246, read/write access 00/00
read NDEF message: ok
  NLEN 68
  record: TNF 2, type 'application/vnd.bluetooth.ep.oob', payload 33 bytes
deselect (token back to RF): ok
I2C errors 0, busy polls 8, WTX 0
```

The 68-byte message is the factory demo's Bluetooth pairing record.

### Writing a link, and opening it with a phone (2026-11-20)

`python build.py run --write-uri "https://github.com/IlaiAhronovitz/h745-rtos"`
wrote a single URI record of 40 bytes. "https://" is stored as prefix code 0x04.
Reading it back matched byte for byte:

```
write NDEF message: ok
read back: ok
verify: ok
  record: TNF 1, type 'U', payload 36 bytes, URI https://github.com/IlaiAhronovitz/h745-rtos
I2C errors 0, busy polls 40 (EEPROM write time), WTX 0
```

A phone tapped on the antenna opened the link. That checks the whole path, from
the I2C session and the deselect handover to the RF side, which the phone reads.

## Build, test, run

```
python build.py test      # host tests: CRC_A vectors, NDEF codec, M24SR flow vs mock tag, src/lc
python build.py quality   # the quality gate: MISRA C, CERT C, strict warnings, sanitizers, coverage
python build.py run       # build, flash, print the report from the board
python build.py read      # read the report again
python build.py restore   # factory demo back
```

The tools are the Arm GNU Toolchain 14.2, xPack OpenOCD 0.12, a host gcc and
Python 3; the quality gate also needs gcc 11, clang 21 and cppcheck 2.16 (Linux, or WSL on Windows).
CI builds the firmware, runs the host tests, and runs the quality gate as a required job
(`.github/workflows/ci.yml`). The coding standard is `docs/CODING-STANDARD.md`; the MISRA C
compliance record and the deviations are in `docs/MISRA-COMPLIANCE.md`.

## Next

1. Find the GPO and RF-disable lines on this board. Then use the GPO interrupt
   to see a phone's write without polling.
2. Show the decoded message on the TFT (SPI2).
3. Protect writes with the M24SR's I2C password.
