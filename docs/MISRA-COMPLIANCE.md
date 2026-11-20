# MISRA C compliance (m24sr-nfc)

This document is the MISRA C:2012 compliance record of m24sr-nfc, in the shape that **MISRA Compliance:2020** asks for:
the Guideline Enforcement Plan, the Guideline Re-categorization Plan, the deviation register (the Guideline Compliance
Summary's supporting records) and the compliance summary. The coding standard itself is `docs/CODING-STANDARD.md`;
the gate that produces the evidence is `tools/quality.sh` (`python build.py quality`).

- **Guidelines:** MISRA C:2012 with Amendments 1, 2, 3 and 4 (the guideline set of MISRA C:2023): 21 directives,
  200 rules. Rules are cited by number and category only; rule texts are not reproduced.
- **Language:** C11 for the target (`-std=c11`), C99 for the portable modules on the host.
- **Scope:** `src/**`: the M24SR-DISCOVERY firmware (`src/*.c`) and the portable Last Checkpoint library (`src/lc/`: host-tested, compiled for Cortex-M, not linked into the image yet). Target: Cortex-M3 (STM32F103RG), `-std=c11 -ffreestanding -nostdlib`, arm-none-eabi-gcc 14.2 in local runs; the CI `quality` job uses Ubuntu 24.04's `gcc-arm-none-eabi` (13.2.rel1). Tests (`tests/`) are not production code: they are held to the warning set and the sanitizers,
  not to MISRA.
- **Category source:** rule categories as listed for MISRA C:2023 in Perforce's QAC enforcement listing
  (help.perforce.com/qac/enforcement/doc/MISRA_M3CM.html), cross-checked for the guideline set against MathWorks'
  Polyspace MISRA C:2023 reference. Directive categories are from the MISRA C:2012 guideline list. Confirm both
  against a licensed copy before any formal claim. Note that Rule 13.6, 17.5 and 21.12 are Required in this set.

## 1. Guideline Enforcement Plan

How each guideline is checked. "cppcheck" means the cppcheck 2.16 MISRA addon implements the rule (`misra_X_Y` in
`addons/misra.py`); its findings are gating. "compiler" and "clang-tidy" name the gating diagnostics that also
catch (part of) the rule. "ASan/UBSan" are the sanitizer runs of every host test and fuzzer. "review" means a person
checks the rule on every change; for rules about features the code does not use, the review confirms they stay
unused (a `grep` is enough: `_Generic`, `<threads.h>`, `<stdatomic.h>`, `<tgmath.h>`, `<stdio.h>`, `malloc`).
cppcheck alone does not decide a rule fully: undecidable rules always also need review.

| Guideline | Category | Enforced by |
|---|---|---|
| Dir 1.1 | Required | review: implementation-defined behaviour listed in CODING-STANDARD.md (gcc/clang, AAPCS, Cortex-M) |
| Dir 2.1 | Required | compiler: every file builds with -Werror for target and host |
| Dir 3.1 | Required | review: requirements trace in README/docs and the tests that cite clauses |
| Dir 4.1 | Required | clang-analyzer, ASan/UBSan, fuzzers |
| Dir 4.2 | Advisory | review (Dir 4.3 records) |
| Dir 4.3 | Required | review; deviation records (inline asm in one header) |
| Dir 4.4 | Advisory | review |
| Dir 4.5 | Advisory | review |
| Dir 4.6 | Advisory | review (stdint.h types; -Wconversion) |
| Dir 4.7 | Required | clang-tidy cert-err33-c, bugprone-unused-return-value; review |
| Dir 4.8 | Advisory | review |
| Dir 4.9 | Advisory | review; clang-tidy bugprone-macro-* |
| Dir 4.10 | Required | review (include guards in every header) |
| Dir 4.11 | Required | clang-tidy bugprone-not-null-terminated-result, host tests |
| Dir 4.12 | Required | review: no heap (no malloc in the code; -nostdlib) |
| Dir 4.13 | Advisory | review |
| Dir 4.14 | Required | review; host tests and fuzzers on every external input |
| Dir 4.15 | Required | review (floating point not used) |
| Dir 5.1 | Required | review: no threads |
| Dir 5.2 | Required | review: no threads |
| Dir 5.3 | Required | review: no threads |
| Rule 1.1 | Required | compiler (-std, -Wpedantic), review |
| Rule 1.2 | Advisory | cppcheck, review |
| Rule 1.3 | Required | clang-tidy (clang-analyzer-core.*), ASan/UBSan, review |
| Rule 1.4 | Required | cppcheck, review |
| Rule 1.5 | Required | review: feature not used; checked in review |
| Rule 2.1 | Required | review |
| Rule 2.2 | Required | cppcheck, compiler (-Wunused), review |
| Rule 2.3 | Advisory | cppcheck, review |
| Rule 2.4 | Advisory | cppcheck, review |
| Rule 2.5 | Advisory | cppcheck, review |
| Rule 2.6 | Advisory | compiler (-Wunused-label), review |
| Rule 2.7 | Advisory | cppcheck, compiler (-Wunused-parameter), review |
| Rule 2.8 | Advisory | review |
| Rule 3.1 | Required | cppcheck, compiler (-Wcomment), review |
| Rule 3.2 | Required | cppcheck, review |
| Rule 4.1 | Required | cppcheck, review |
| Rule 4.2 | Advisory | cppcheck, compiler (-Wtrigraphs), review |
| Rule 5.1 | Required | cppcheck, review |
| Rule 5.2 | Required | cppcheck, review |
| Rule 5.3 | Required | compiler (-Wshadow), review |
| Rule 5.4 | Required | cppcheck, review |
| Rule 5.5 | Required | cppcheck, review |
| Rule 5.6 | Required | cppcheck, review |
| Rule 5.7 | Required | cppcheck, review |
| Rule 5.8 | Required | cppcheck, review |
| Rule 5.9 | Advisory | cppcheck, review |
| Rule 6.1 | Required | cppcheck, review |
| Rule 6.2 | Required | cppcheck, review |
| Rule 6.3 | Required | review |
| Rule 7.1 | Required | cppcheck, review |
| Rule 7.2 | Required | cppcheck, review |
| Rule 7.3 | Required | cppcheck, review |
| Rule 7.4 | Required | cppcheck, review |
| Rule 7.5 | Mandatory | review |
| Rule 7.6 | Required | review |
| Rule 8.1 | Required | cppcheck, compiler (C99 (no implicit int)), review |
| Rule 8.2 | Required | cppcheck, compiler (-Wstrict-prototypes), review |
| Rule 8.3 | Required | review |
| Rule 8.4 | Required | cppcheck, compiler (-Wmissing-prototypes, -Wmissing-declarations), review |
| Rule 8.5 | Required | cppcheck, review |
| Rule 8.6 | Required | cppcheck, review |
| Rule 8.7 | Advisory | cppcheck, review |
| Rule 8.8 | Required | cppcheck, compiler (-Wredundant-decls), review |
| Rule 8.9 | Advisory | cppcheck, review |
| Rule 8.10 | Required | cppcheck, review |
| Rule 8.11 | Advisory | cppcheck, review |
| Rule 8.12 | Required | cppcheck, review |
| Rule 8.13 | Advisory | review |
| Rule 8.14 | Required | cppcheck, review |
| Rule 8.15 | Required | review |
| Rule 8.16 | Advisory | review |
| Rule 8.17 | Advisory | review |
| Rule 9.1 | Mandatory | compiler (-Wuninitialized), clang-tidy (clang-analyzer-core.uninitialized.*), ASan/UBSan (-O1), review |
| Rule 9.2 | Required | cppcheck, review |
| Rule 9.3 | Required | cppcheck, review |
| Rule 9.4 | Required | cppcheck, review |
| Rule 9.5 | Required | cppcheck, review |
| Rule 9.6 | Required | review: feature not used; checked in review |
| Rule 9.7 | Mandatory | review: feature not used; checked in review |
| Rule 10.1 | Required | cppcheck, compiler (-Wconversion), UBSan, review |
| Rule 10.2 | Required | cppcheck, review |
| Rule 10.3 | Required | cppcheck, compiler (-Wconversion), clang-tidy (bugprone-narrowing-conversions), review |
| Rule 10.4 | Required | cppcheck, compiler (-Wsign-conversion), review |
| Rule 10.5 | Advisory | cppcheck, review |
| Rule 10.6 | Required | cppcheck, review |
| Rule 10.7 | Required | cppcheck, review |
| Rule 10.8 | Required | cppcheck, compiler (-Wconversion), clang-tidy (bugprone-misplaced-widening-cast), review |
| Rule 11.1 | Required | cppcheck, review |
| Rule 11.2 | Required | cppcheck, review |
| Rule 11.3 | Required | cppcheck, compiler (-Wcast-align=strict), review |
| Rule 11.4 | Advisory | cppcheck, review |
| Rule 11.5 | Advisory | cppcheck, review |
| Rule 11.6 | Required | cppcheck, review |
| Rule 11.7 | Required | cppcheck, review |
| Rule 11.8 | Required | cppcheck, compiler (-Wcast-qual), review |
| Rule 11.9 | Required | cppcheck, review |
| Rule 11.10 | Required | review |
| Rule 12.1 | Advisory | cppcheck, review |
| Rule 12.2 | Required | cppcheck, compiler (-Wshift-count-overflow), clang-tidy (clang-analyzer-core.UndefinedBinaryOperatorResult), UBSan shift, review |
| Rule 12.3 | Advisory | cppcheck, review |
| Rule 12.4 | Advisory | cppcheck, review |
| Rule 12.5 | Mandatory | review |
| Rule 12.6 | Required | review |
| Rule 13.1 | Required | cppcheck, review |
| Rule 13.2 | Required | compiler (-Wsequence-point / -Wunsequenced), review |
| Rule 13.3 | Advisory | cppcheck, review |
| Rule 13.4 | Advisory | cppcheck, clang-tidy (bugprone-assignment-in-if-condition), review |
| Rule 13.5 | Required | cppcheck, review |
| Rule 13.6 | Required | cppcheck, review |
| Rule 14.1 | Required | cppcheck, clang-tidy (cert-flp30-c), review |
| Rule 14.2 | Required | cppcheck, review |
| Rule 14.3 | Required | compiler (-Wtype-limits (in -Wextra)), review |
| Rule 14.4 | Required | cppcheck, review |
| Rule 15.1 | Advisory | cppcheck, review |
| Rule 15.2 | Required | cppcheck, review |
| Rule 15.3 | Required | cppcheck, review |
| Rule 15.4 | Advisory | cppcheck, review |
| Rule 15.5 | Advisory | cppcheck, review **(disapplied: see the Re-categorization Plan)** |
| Rule 15.6 | Required | cppcheck, clang-tidy (readability-braces-around-statements), review |
| Rule 15.7 | Required | cppcheck, review |
| Rule 16.1 | Required | cppcheck, review |
| Rule 16.2 | Required | cppcheck, review |
| Rule 16.3 | Required | cppcheck, compiler (-Wimplicit-fallthrough (gcc -Wextra)), review |
| Rule 16.4 | Required | cppcheck, compiler (-Wswitch-default), review |
| Rule 16.5 | Required | cppcheck, review |
| Rule 16.6 | Required | cppcheck, compiler (-Wswitch), review |
| Rule 16.7 | Required | cppcheck, review |
| Rule 17.1 | Required | cppcheck, review |
| Rule 17.2 | Required | cppcheck, clang-tidy (misc-no-recursion), review |
| Rule 17.3 | Mandatory | cppcheck, compiler (C99 (implicit declaration)), review |
| Rule 17.4 | Mandatory | compiler (-Wreturn-type), review |
| Rule 17.5 | Required | review |
| Rule 17.6 | Mandatory | cppcheck, review |
| Rule 17.7 | Required | cppcheck, clang-tidy (cert-err33-c, bugprone-unused-return-value), review |
| Rule 17.8 | Advisory | cppcheck, review |
| Rule 17.9 | Mandatory | review: no _Noreturn functions |
| Rule 17.10 | Required | review: no _Noreturn functions |
| Rule 17.11 | Advisory | review |
| Rule 17.12 | Advisory | review |
| Rule 17.13 | Required | review |
| Rule 18.1 | Required | clang-tidy (clang-analyzer-security.ArrayBound, clang-analyzer-core.*), ASan, review |
| Rule 18.2 | Required | clang-tidy (clang-analyzer-core.*), ASan, review |
| Rule 18.3 | Required | ASan, review |
| Rule 18.4 | Advisory | cppcheck, review |
| Rule 18.5 | Advisory | cppcheck, review |
| Rule 18.6 | Required | clang-tidy (clang-analyzer-core.StackAddressEscape), review |
| Rule 18.7 | Required | cppcheck, review |
| Rule 18.8 | Required | cppcheck, compiler (-Wvla), review |
| Rule 18.9 | Required | review: feature not used; checked in review |
| Rule 18.10 | Mandatory | review: feature not used; checked in review |
| Rule 19.1 | Mandatory | review |
| Rule 19.2 | Advisory | cppcheck, review |
| Rule 20.1 | Advisory | cppcheck, review |
| Rule 20.2 | Required | cppcheck, review |
| Rule 20.3 | Required | cppcheck, review |
| Rule 20.4 | Required | cppcheck, review |
| Rule 20.5 | Advisory | cppcheck, review |
| Rule 20.6 | Required | review |
| Rule 20.7 | Required | cppcheck, clang-tidy (bugprone-macro-parentheses), review |
| Rule 20.8 | Required | cppcheck, review |
| Rule 20.9 | Required | cppcheck, compiler (-Wundef), review |
| Rule 20.10 | Advisory | cppcheck, review |
| Rule 20.11 | Required | cppcheck, review |
| Rule 20.12 | Required | cppcheck, review |
| Rule 20.13 | Required | cppcheck, review |
| Rule 20.14 | Required | cppcheck, review |
| Rule 21.1 | Required | cppcheck, review |
| Rule 21.2 | Required | cppcheck, clang-tidy (bugprone-reserved-identifier, cert-dcl37-c), review |
| Rule 21.3 | Required | cppcheck, compiler (-nostdlib (target)), review |
| Rule 21.4 | Required | cppcheck, review |
| Rule 21.5 | Required | cppcheck, compiler (-nostdlib (target)), review |
| Rule 21.6 | Required | cppcheck, compiler (-nostdlib (target)), review |
| Rule 21.7 | Required | cppcheck, review |
| Rule 21.8 | Required | cppcheck, compiler (-nostdlib (target)), review |
| Rule 21.9 | Required | cppcheck, review |
| Rule 21.10 | Required | cppcheck, compiler (-nostdlib (target)), review |
| Rule 21.11 | Advisory | cppcheck, review |
| Rule 21.12 | Required | cppcheck, review |
| Rule 21.13 | Mandatory | review |
| Rule 21.14 | Required | cppcheck, review |
| Rule 21.15 | Required | cppcheck, review |
| Rule 21.16 | Required | cppcheck, review |
| Rule 21.17 | Mandatory | clang-tidy (clang-analyzer-unix.cstring.*), review |
| Rule 21.18 | Mandatory | clang-tidy (clang-analyzer-unix.cstring.*), review |
| Rule 21.19 | Mandatory | cppcheck, review |
| Rule 21.20 | Mandatory | cppcheck, review |
| Rule 21.21 | Required | cppcheck, review |
| Rule 21.22 | Mandatory | review: no <tgmath.h> or type-generic math |
| Rule 21.23 | Required | review: no <tgmath.h> or type-generic math |
| Rule 21.24 | Required | review: no <tgmath.h> or type-generic math |
| Rule 21.25 | Required | review: no threads, atomics or mutexes in the code |
| Rule 21.26 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.1 | Required | clang-tidy (clang-analyzer-unix.Malloc), review |
| Rule 22.2 | Mandatory | clang-tidy (clang-analyzer-unix.Malloc), review |
| Rule 22.3 | Required | review: no file I/O (no <stdio.h>) |
| Rule 22.4 | Mandatory | review: no file I/O |
| Rule 22.5 | Mandatory | cppcheck, review |
| Rule 22.6 | Mandatory | review: no file I/O |
| Rule 22.7 | Required | cppcheck, review |
| Rule 22.8 | Required | cppcheck, review |
| Rule 22.9 | Required | cppcheck, review |
| Rule 22.10 | Required | cppcheck, review |
| Rule 22.11 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.12 | Mandatory | review: no threads, atomics or mutexes in the code |
| Rule 22.13 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.14 | Mandatory | review: no threads, atomics or mutexes in the code |
| Rule 22.15 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.16 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.17 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.18 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.19 | Required | review: no threads, atomics or mutexes in the code |
| Rule 22.20 | Mandatory | review: no threads, atomics or mutexes in the code |
| Rule 23.1 | Advisory | review: no _Generic in the code |
| Rule 23.2 | Required | review: no _Generic in the code |
| Rule 23.3 | Advisory | review: no _Generic in the code |
| Rule 23.4 | Required | review: no _Generic in the code |
| Rule 23.5 | Advisory | review: no _Generic in the code |
| Rule 23.6 | Required | review: no _Generic in the code |
| Rule 23.7 | Advisory | review: no _Generic in the code |
| Rule 23.8 | Required | review: no _Generic in the code |

## 2. Guideline Re-categorization Plan

MISRA Compliance:2020 allows a project to raise any category, and to disapply an Advisory guideline. Mandatory and
Required guidelines are never lowered here.

| Guideline | MISRA category | Project category | Reason |
|---|---|---|---|
| Rule 15.5 | Advisory | Disapplied | The code validates untrusted input field by field (frames, modem text, records) and brings up hardware in ordered steps. A guard clause that returns the step's error at the failing check keeps each check next to its error code and keeps the order of register writes visible. A single exit would need a status flag tested before every step, which nests or repeats the condition and pushes functions past the cognitive-complexity limit of 25 that clang-tidy enforces. No function holds a resource that must be released on exit (no heap, no locks, no files), so an early return cannot leak. Requested in C-1 (`docs/deviations/common.md`); applied in `tools/quality.sh` (`DISAPPLIED`). |

All other guidelines keep their MISRA category. The per-file suppressions of Advisory Rules 2.5 and 8.7 stay as
deviation records, because they are narrow (named headers and public APIs), not project-wide.

## 3. Deviation register

Each record covers one pattern (not one line) and has: an ID; the guideline numbers with categories and the tool
check IDs; where it applies (files, macros, functions); why it is needed; the risk and the safeguards; how it is
verified (tests, fuzzers, board run); and the suppressions it authorises. cppcheck suppressions are one rule in one
file, in `tools/quality/suppress-<area>.txt`; clang-tidy suppressions are `NOLINT(<check>)` comments that name the
record. There are no deviations of Mandatory guidelines. How to add one: `docs/CODING-STANDARD.md`, section 4.

| Record file | Records | Guidelines cited |
|---|---|---|
| [`docs/deviations/common.md`](deviations/common.md) | C-1, C-2, C-3 | Rule 2.5, Rule 8.7, Rule 15.5 (disapplied, section 2) |
| [`docs/deviations/target.md`](deviations/target.md) | T-1, T-2, T-3, T-4 | Directive 4.3, Rule 8.7, Rule 11.4, Rule 11.5, Rule 21.2 |

The column lists the guidelines each file deviates. The records also cite rules that hold (Rule 8.4) or are not
triggered (Rule 11.6); those are not deviations.

Suppression files: `tools/quality/suppress-common.txt` (9 lines) and `tools/quality/suppress-target.txt` (10 lines):
19 suppression lines, plus the disapplied Rule 15.5, which `tools/quality.sh` adds once from `DISAPPLIED`. State on 2026-11-20; the area work was still in
progress, so this index lists the records that existed when the gate last ran.

## 4. Compliance summary

From `tools/quality.sh` run on 2026-11-20, in two steps, because WSL could not start the Windows Arm toolchain on
the machine used (WSL interop off): every check but `target-gcc` in WSL (Ubuntu 22.04) with
`--allow-skip target-gcc`, then `--only target-gcc` in Git Bash with the native Arm toolchain. Tools:

```
  arm-gcc         Arm GNU Toolchain 14.2.Rel1 (arm-none-eabi-gcc 14.2.1 20241119), Git Bash step
  gcc             gcc-11 (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
  clang           Ubuntu clang version 21.1.8
  clang-tidy      Ubuntu LLVM version 21.1.8
  llvm-cov        Ubuntu LLVM version 21.1.8
  cppcheck        Cppcheck 2.16.0
```

```
=== summary: m24sr-nfc                       (WSL, --allow-skip target-gcc)
check            files  findings  status   note
--------------- ------ ---------  -------- ----
target-gcc          10         -  SKIPPED  allowed by --allow-skip: no arm-none-eabi-gcc (WSL interop off)
target-clang        10         0  PASS
host-gcc             9         0  PASS
host-clang           9         0  PASS
misra               10         0  PASS
clang-tidy          10         0  PASS
sanitize-gcc         3         0  PASS     programs built+run clean: 3
sanitize-clang       3         0  PASS     programs built+run clean: 3
coverage             5         0  PASS     lines 99.46%, branches 90.97%
allowed to skip (--allow-skip): target-gcc
RESULT: PASS, except skipped (allowed): target-gcc

=== summary: m24sr-nfc                       (Git Bash, --only target-gcc)
target-gcc          10         0  PASS     max frame 304 B
RESULT: PASS
```

MISRA findings still open, by category: Mandatory 0, Required 0,
Advisory 0 (0 MISRA findings in total; any other findings in the misra row are
cppcheck's own warning/style checks).

**Compliance status: tool-checked guidelines clean.** Every gate check passes, and every suppression left is covered by a record in section 3. A full claim per MISRA Compliance:2020 still needs the review-only guidelines (the rows marked "review" in section 1) reviewed and recorded, and the categories confirmed against a licensed copy.
