# Coding standard

m24sr-nfc is held to one standard, checked by one script. "Done" means every check below runs clean, and anything
left over is a documented deviation with a reason. Findings are not hidden: no blanket suppressions, no `#pragma`
to silence a tool, no disabled checks. A suppression exists only when a deviation record covers it.

```sh
python build.py quality              # on Windows (runs in WSL), or on Linux
bash tools/quality.sh                # in WSL or Linux; --only <check> runs one check, --list names them
python build.py test                 # just the host tests, with the host gcc
```

**A skipped check is a failed check.** If a tool cannot run, its row says `SKIPPED` and the gate fails. A local run
may accept a skip only by naming it, `--allow-skip <check>`, which the summary prints; the skipped check must then run
another way. The one case today is WSL with the Windows Arm toolchain and WSL interop off: run
`python build.py quality --allow-skip target-gcc`, then `bash tools/quality.sh --only target-gcc` in Git Bash. CI never
passes `--allow-skip` (the script refuses it when `CI` is set). Details: `tools/quality/README.md`.

CI runs `tools/quality.sh` as the required `quality` job (`.github/workflows/ci.yml`). What each check runs, which
files it covers and how tools are found: `tools/quality/README.md`. MISRA C compliance, the deviation register and the
latest results: `docs/MISRA-COMPLIANCE.md`.

## 1. What is applied

| Area | Standard | Checked by |
|---|---|---|
| C language subset | **MISRA C:2012** with Amendments 1 to 4, claimed per **MISRA Compliance:2020** | cppcheck 2.16 `--addon=misra`, the compilers, clang-tidy, review |
| Secure coding | **SEI CERT C** | clang-tidy 21: `cert-*`, `bugprone-*`, `clang-analyzer-*` |
| Warnings | the strict set below, as errors | arm-none-eabi-gcc (target: 14.2 in local runs, Ubuntu 24.04's 13.2 in CI), gcc 11 and clang 21 (host and target syntax) |
| Runtime defects | no AddressSanitizer or UndefinedBehaviorSanitizer finding in any host test or fuzz run | gcc 11 and clang 21 sanitizers |
| Tests | every portable module has host tests; line and branch coverage reported per file | clang source-based coverage |
| Clean code | short functions named for what they do; cognitive complexity at most 25; at most 80 lines per function | clang-tidy `readability-*` |
| Protocols | ISO/IEC 14443-3 (CRC_A) and 14443-4 (block protocol: I-, R- and S-blocks, block numbering, WTX, DESELECT); NFC Forum Type 4 Tag and NDEF, with the URI and Text record types; 3GPP TS 24.301, 24.008 and 27.007 for the modem text the `src/lc/` parser reads. The versions are pinned in the code. | review, and host tests that cite the clause |

MISRA rule texts are copyrighted. This repository cites rules by number ("Rule 10.4") and describes them in its own
words; it never quotes rule text.

### MISRA categories and what they allow

- **Mandatory:** never deviated.
- **Required:** broken only with a deviation record.
- **Advisory:** may be disapplied for the whole project, but only in the Guideline Re-categorization Plan in
  `docs/MISRA-COMPLIANCE.md`, with a reason. Otherwise treated like Required.

Hardware access is a known, accepted deviation class: casting an integer constant to a `volatile` pointer for a
memory-mapped register (Rule 11.4, plus 11.6 where it applies) and the `dsb`/`isb`/`wfi` inline assembly
(Directive 4.3, kept in one header), and `libmin.c`'s `memset`/`memcpy` in a `-nostdlib` build (Rule 21.2).
Each such pattern is recorded once, not once per line.

## 2. The checks

The strict warning set, added to each build's own flags:

```
WARN     = -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wstrict-prototypes
           -Wmissing-prototypes -Wmissing-declarations -Wundef -Wdouble-promotion -Wformat=2 -Wnull-dereference
           -Wredundant-decls -Wvla -Wswitch-default -Werror
GCC_ONLY = -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init -Wcast-align=strict
```

| Check | What it does |
|---|---|
| Target code, gcc | arm-none-eabi-gcc with `build.py`'s target flags + `WARN` + `GCC_ONLY` + `-fstack-usage -Wstack-usage=1024`, compiled to a scratch directory (never `build/`). No stack frame over 1 KiB. |
| Target code, clang | `clang-21 --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -std=c11 -Isrc` + `WARN`, `-fsyntax-only` (the Arm toolchain's newlib headers as `-isystem`). |
| Portable code and tests | gcc 11 and clang 21 on the host: portable code as C99, tests as C11, `WARN` (+ `GCC_ONLY` for gcc). |
| MISRA C:2012 | `cppcheck --addon=misra --std=c11 --platform=unix32 --enable=warning,style,performance,portability --inline-suppr --error-exitcode=1 -Isrc --suppressions-list=<all tools/quality/suppress-*.txt>` over the production code. cppcheck prints rule numbers only, with no rule texts file: that is expected. |
| CERT C and bug finding | clang-tidy 21 with `.clang-tidy`: `bugprone-*`, `cert-*`, `clang-analyzer-*`, `misc-*`, `performance-*`, `portability-*`, `readability-*`, minus `readability-magic-numbers`, `readability-identifier-length`, `misc-include-cleaner`, `bugprone-easily-swappable-parameters`, `readability-avoid-unconditional-preprocessor-if`; cognitive complexity threshold 25, function size 80 lines; every warning is an error. |
| Sanitizers | every host test and fuzzer built with `-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1` under gcc 11 and clang 21, and run. Fuzzers run their full standalone iteration count. |
| Coverage | `clang-21 -fprofile-instr-generate -fcoverage-mapping`, run the unit tests, `llvm-profdata-21 merge`, `llvm-cov-21 report` (lines and branches per file). The aim is 100 % of reachable lines in portable modules; every uncovered branch left is explained in the area's notes. |

**Scope.** Production code is `src/**`: the M24SR-DISCOVERY firmware (`src/*.c`) and the Last Checkpoint library
(`src/lc/`: host-tested, compiled for Cortex-M by the gate and by `build.py test`, but not linked into the image yet). MISRA and clang-tidy apply to production code. The
warning set and the sanitizers apply to production code and tests. Portable code is the part that also builds and is
tested on the host: `crc_a.c`, `ndef.c`, `m24sr.c` (over the I2C mock in `tests/mock_m24sr.c`) and `src/lc/`. The
`src/lc/` parsers take untrusted modem text: CERT C findings there are treated as real until shown otherwise.

## 3. Rules for changes

- **Behaviour does not change** unless the change fixes a real defect or a standard violation. Every existing test
  still passes. Each fixed defect gets a test that names the clause or rule it enforces.
- **Target code** cannot be run in CI. Changes there stay mechanical and safe: types, casts, `const`, explicit
  widths, named constants. Register writes are never reordered, barriers are never removed, and the I2C read
  sequences of RM0008 (one byte, two bytes, more than two) keep their exact order. Each target-side
  change is listed in the change's notes so it is re-verified on the board.
- **Clean code:** small functions with clear names; split a function before it reaches the complexity or size limit.

## 4. How to add a deviation

A deviation is needed to break a Required rule (Mandatory rules cannot be deviated), or to keep any tool finding that
is correct by design.

1. **Write the record** in `docs/deviations/<area>.md`, one record per pattern, not per line. It has:
   - an **ID** (`C-1`, `T-2`, ...);
   - the **guideline(s)** by number, with category, and the tool check IDs (for example `misra-c2012-11.4`,
     `performance-no-int-to-ptr`);
   - **where**: the files, macros or functions the pattern covers;
   - **why** it is needed: why compliant code is not possible or would be worse;
   - **risk and safeguards**: what could go wrong and what prevents it (tests, asserts, confinement to one macro);
   - **verification**: the test, fuzzer or board run that covers it;
   - the **suppressions** it authorises.
2. **Add the narrowest suppression:**
   - cppcheck: one line per rule and file in `tools/quality/suppress-<area>.txt`, such as `misra-c2012-11.4:src/regs.h`.
     No `*` patterns.
   - clang-tidy: `// NOLINT(<check>): <record ID>` on the line, or `NOLINTBEGIN/NOLINTEND(<check>)` around a block.
3. **Index it:** the deviation register in `docs/MISRA-COMPLIANCE.md` links every `docs/deviations/*.md` file.
4. **Run** `python build.py quality` and confirm the finding is gone and nothing else is suppressed.

To disapply an Advisory rule for the whole project, add it to the Guideline Re-categorization Plan in
`docs/MISRA-COMPLIANCE.md` with the reason, and to `DISAPPLIED` in `tools/quality.sh`.
