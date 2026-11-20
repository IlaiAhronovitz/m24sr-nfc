# Deviation records: target code (m24sr-nfc, STM32F103 on the M24SR-DISCOVERY)

Scope: the code that runs only on the board: `src/regs.h`, `src/i2c.c`, `src/system.c`, `src/report.c`,
`src/main.c`, `src/libmin.c` and `src/startup.s`. MISRA rules are cited by number; the descriptions are
our own words. MISRA suppressions are in `tools/quality/suppress-target.txt`; every line there belongs to
one record below. clang-tidy suppressions are `NOLINT` comments that name the check, and each one is
listed here. Project-wide patterns (Rules 15.5, 8.7 and 2.5 on the portable API) are in `common.md`.

---

## T-1: Rule 11.4 (Advisory): memory-mapped registers at fixed addresses

| | |
|---|---|
| Rule | MISRA C:2012 Rule 11.4, Advisory (conversion between an integer and a pointer to an object). Rule 11.6 is not triggered: no `void *` is involved. Also clang-tidy `performance-no-int-to-ptr` and `clang-analyzer-core.FixedAddressDereference`. |
| Where | The casts exist only in `src/regs.h`: `REG32(a)` (core, SCB, SysTick, DBGMCU and RCC registers), `GPIOB` and `I2C2` (register blocks as structs of `volatile uint32_t`). They are reported where the macros expand: `i2c.c` (RCC, GPIOB and I2C2 accesses), `system.c` (VTOR, SysTick), `report.c` (`fault_report()` reads CFSR), `main.c` (DBGMCU_CR). `system_init()` also converts the address of the vector table `g_vectors` to an integer to write it into SCB_VTOR, which the register needs. |
| Why | Peripheral registers sit at fixed addresses given by RM0008 and the Cortex-M3 TRM. C has no other way to reach them than a pointer made from that address. |
| Why it is safe | Every address is a named constant copied from RM0008 (section noted next to each block) or the ARM v7-M ARM. The pointers are `volatile`, so every access is emitted in program order. The struct layouts match the register maps (all 32-bit, no padding). No pointer arithmetic is done on these pointers. |
| Control | One place: an integer-to-pointer cast outside `regs.h` is a review finding (grep for `(uintptr_t)` and `*)0x`). `regs.h` holds only the registers the code uses. The disassembly of every register-touching function was compared (`arm-none-eabi-objdump -d`, build flags minus `-g3`) before and after the 2026-11 quality changes: `i2c.c` (including the `i2c2_read` split, whose helpers inline back), `system.c` and `main.c` compile to identical code (only static label names differ); `report.c`'s `put()` and `log_hex()` differ in register allocation and loop shape only, with no register access. |
| MISRA suppressions | `misra-c2012-11.4:` `src/regs.h`, `src/i2c.c`, `src/system.c`, `src/report.c`, `src/main.c`. |
| clang-tidy suppressions | `NOLINT(performance-no-int-to-ptr)` on the three macro lines in `regs.h` (`REG32`, `GPIOB`, `I2C2`). `NOLINTBEGIN/NOLINTEND(clang-analyzer-core.FixedAddressDereference)` around the bodies of `i2c.c` and `system.c` (the analyzer reports each register read through a fixed address). `NOLINTNEXTLINE(clang-analyzer-core.FixedAddressDereference)` on the DBGMCU_CR write in `main()` and on the CFSR read in `fault_report()`. |

## T-2: Directive 4.3 (Required): assembly language

| | |
|---|---|
| Rule | MISRA C:2012 Directive 4.3, Required (assembly shall be encapsulated and isolated). |
| Where | `src/regs.h`: `irq_off()` (`cpsid i`), `irq_on()` (`cpsie i`) and `cpu_wfi()` (`wfi`), each a one-instruction `static inline` function. `src/startup.s`: the vector table, `Reset_Handler` (stack, `.data` copy, `.bss` clear, call to `main`) and `Fault_Handler` (picks MSP or PSP and calls `fault_report()`). |
| Why | Masking interrupts around the RM0008 26.3.3 read windows, sleeping in `main()`'s idle loop, and the reset and fault entry points have no C equivalent. |
| Why it is safe | The C side never contains inline assembly outside these three functions in `regs.h`; the callers only see a C function call. The two IRQ functions carry a `"memory"` clobber, so the compiler does not move memory accesses across them. `startup.s` is plain Thumb-2 for the Cortex-M3 and uses no C object other than the linker symbols and `main`/`fault_report`. |
| Control | A new `__asm` anywhere else is a review finding (grep `__asm`). cppcheck does not check this directive; it is a review item. |
| Suppressions | None needed (no tool reports it). |

## T-3: Rule 21.2 (Required), Rule 11.5 (Advisory), Rule 8.7 (Advisory): `libmin.c`

| | |
|---|---|
| Rule | Rule 21.2, Required (reserved identifiers); Rule 11.5, Advisory (conversion from `void *` to an object pointer); Rule 8.7, Advisory (external linkage used in one unit). |
| Where | `src/libmin.c`: `memset()` and `memcpy()`. |
| Why | The image is linked with `-nostdlib`. GCC may still emit calls to `memset` and `memcpy` on its own, for structure copies and clears (for example the zero-initialised `m24sr_t` or a struct assignment), even when the source never calls them. These two definitions must therefore have exactly the standard names and the standard `void *` signatures, which is why they reuse reserved identifiers (21.2), convert `void *` to `uint8_t *` inside (11.5), and have external linkage that no C unit in `src/` uses (8.7: the callers are compiler-generated). |
| Why it is safe | They implement exactly the standard behaviour for non-overlapping buffers, byte by byte. Prototypes are declared before the definitions (Rule 8.4 holds). The disassembly was checked: neither function calls itself (no loop-to-`memset` idiom recognition at `-Os -ffreestanding`). No hosted C library is linked, so there is no second definition. |
| Control | `libmin.c` holds only these two functions. Adding another standard-library name needs a new record. |
| Suppressions | `misra-c2012-21.2:src/libmin.c`, `misra-c2012-11.5:src/libmin.c`, `misra-c2012-8.7:src/libmin.c`. |

## T-4: Rule 8.7 (Advisory): functions entered from `startup.s`

| | |
|---|---|
| Rule | MISRA C:2012 Rule 8.7, Advisory. |
| Where | `SysTick_Handler()` in `src/system.c` (vector table entry 15) and `fault_report()` in `src/report.c` (branched to from `Fault_Handler`). |
| Why | Both are referenced only from assembly (`startup.s`), which cppcheck does not see, so it reports that they could have internal linkage. Making them `static` would break the link. |
| Why it is safe | Each has one prototype in its header (`system.h`, `report.h`), so Rule 8.4 holds and the assembly and C agree on the signature (`void (void)` and `void (const uint32_t *)`, argument in r0 per the AAPCS). |
| Control | The suppression is per file, so it would also hide a new unused external function in `system.c` or `report.c`. Review of those two files checks that every external function there has a user in `src/` or is one of these two. |
| Suppressions | `misra-c2012-8.7:src/system.c`, `misra-c2012-8.7:src/report.c`. |
