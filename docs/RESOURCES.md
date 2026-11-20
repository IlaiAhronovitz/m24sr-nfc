# Resources: m24sr-nfc

Measured from a scratch build on 2026-11-20 (a copy of the tree under `%TEMP%`, Arm GNU Toolchain 14.2 rel1):

```
python build.py        # build.py's own flags, plus -fstack-usage added for the stack table
arm-none-eabi-size -A build/m24sr-nfc.elf
```

Flags (from `build.py`): `-mcpu=cortex-m3 -mthumb -Os -g3 -std=c11 -Wall -Wextra -Werror`, linked with
`-nostartfiles -nostdlib -Wl,--gc-sections`. `src/lc/` is host-only and is not in the image.
Numbers change with every commit: rebuild before quoting them.

## Flash and RAM per memory region

Linker script: `linker/stm32f103rg.ld` (STM32F103RG, XL density). Sizes in bytes.

| Region | Address | Size | Used | Content |
|---|---|---|---|---|
| FLASH | `0x08000000` | 1 MiB | 4752 (0.5 %) | `.vectors` 64, `.text` + `.rodata` 4688, `.data` 0 |
| REPORT | `0x20000000` | 4 KiB | 2128 (52 %) | `.report`: status block and text log, read over SWD |
| RAM | `0x20001000` | 92 KiB | 12 | `.data` 0 + `.bss` 12; the rest is stack (grows down from `0x20018000`) |

The linker ASSERTs at least 8 KiB between `.bss` and the top of RAM. Stack use itself is not measured.
Total image: text 4752, data 0, bss 2140 (includes the report block).

## Largest stack frames

From `-fstack-usage` (all static). The gate's limit is 1 KiB per function.

| Frame (bytes) | Function | File |
|---|---|---|
| 304 | `main` | `src/main.c:206` (holds the `m24sr_t` state) |
| 120 | `m24sr_update_binary.part.0` | `src/m24sr.c:242` |
| 120 | `exchange` | `src/m24sr.c:115` |
| 104 | `ndef_build_text` | `src/ndef.c:216` |
| 56 | `ndef_build_uri` | `src/ndef.c:202` |
| 48 | `m24sr_read_binary.part.0` | `src/m24sr.c:221` |

The worst call chain depth is not measured.

## DMA areas and MPU regions

None. The F103 has no MPU and no DMA channel is used: all I2C transfers are polled, byte by byte.

## Clocks

| Clock | Value | Source |
|---|---|---|
| SYSCLK = HCLK = PCLK1 | 8 MHz | HSI, no PLL, no crystal |
| I2C2 SCL | 100 kHz | standard mode: `CCR = 40`, `CR2 = 8` (PCLK1 in MHz), `TRISE = 9` |
| SysTick | 8 MHz (8 ticks per microsecond) | millisecond tick and short delays |

The factory firmware ran at 72 MHz; this firmware does not.

## Peripherals used

- I2C2 on PB10 (SCL) and PB11 (SDA), open-drain alternate function, polled. Bus recovery drives both pins as GPIO
  (9 clocks and a STOP) before the peripheral is enabled.
- RCC (GPIOB and I2C2 clocks, I2C2 reset), SysTick, DBGMCU (debug kept alive in sleep).
- Off-chip: the M24SR64-Y tag (I2C address and token handling in `src/m24sr.c`).
- Not used: SPI2 (TFT), the GPO and RF-disable lines (not found yet), joystick, LEDs, USB, DMA, any timer but SysTick.

## Power

Power: not measured yet.
