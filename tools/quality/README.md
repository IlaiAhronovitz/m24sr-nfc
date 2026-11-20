# tools/quality: the quality gate

`tools/quality.sh` runs every check of the coding standard (`docs/CODING-STANDARD.md`) over the repository and
prints one summary table. It exits non-zero when any check has a finding, or when any check was SKIPPED and the run
did not allow it. CI runs it as the required `quality` job.

```sh
python build.py quality                  # Windows: runs it in WSL (QUALITY_WSL_DISTRO names your distro)
bash tools/quality.sh                    # WSL or Linux, from anywhere
bash tools/quality.sh --only misra       # one check; --list prints the names
bash tools/quality.sh --keep             # keep the scratch dir: full logs, objects, .su files, coverage data
bash tools/quality.sh --allow-skip target-gcc   # local only: a SKIPPED target-gcc does not fail this run
```

## Skipped checks fail the gate

A check that could not run proves nothing, so a `SKIPPED` row is a `FAIL` unless the run names that check with
`--allow-skip <check>` (repeatable; `python build.py quality --allow-skip <check>` passes it on). An allowed skip
stays `SKIPPED` in the table, the summary prints `allowed to skip (--allow-skip): ...`, and the last line is
`RESULT: PASS, except skipped (allowed): <checks>`. The script refuses `--allow-skip` when the `CI` environment
variable is set (GitHub Actions sets `CI=true`), and the CI workflow never passes it: in CI every check runs.

Today only `target-gcc` can be skipped: when no `arm-none-eabi-gcc` runs. That happens in WSL when the Arm toolchain
is the Windows one and WSL interop is off (`/proc/sys/fs/binfmt_misc/WSLInterop` missing). A local run then has two
steps, and both must pass:

```sh
python build.py quality --allow-skip target-gcc    # every other check, in WSL
bash tools/quality.sh --only target-gcc             # in Git Bash on Windows: the native Arm gcc (found via cygpath)
```

Nothing is written into the repository. Objects, logs and profiles go to a `mktemp` directory that is deleted at the
end unless you pass `--keep`.

## The checks

| Check | Tool | Files | Command (flags beyond these: see the script) |
|---|---|---|---|
| `target-gcc` | arm-none-eabi-gcc | production | `build.py`'s `ARCH` + `CFLAGS` (read from `build.py`) + `WARN` + `GCC_ONLY` + `-fstack-usage -Wstack-usage=1024`, `-c` into scratch |
| `target-clang` | clang 21 | production | `--target=arm-none-eabi -mcpu=cortex-m3 -mthumb -ffreestanding -std=c11 -Isrc` + `WARN`, `-fsyntax-only`; the Arm toolchain's newlib headers as `-isystem` |
| `host-gcc` | gcc 11 | portable + tests | portable `-std=c99`, tests `-std=c11`, `WARN` + `GCC_ONLY`, `-fsyntax-only` |
| `host-clang` | clang 21 | portable + tests | the same, without `GCC_ONLY` |
| `misra` | cppcheck 2.16 `--addon=misra` | production | the standard's command; suppressions = all `tools/quality/suppress-*.txt` + the disapplied advisory rules |
| `clang-tidy` | clang-tidy 21 | production | `.clang-tidy` (the standard's check list and options), target compile flags + `WARN` |
| `sanitize-gcc` | gcc 11 | test programs | `-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1`, build and run every test and fuzzer |
| `sanitize-clang` | clang 21 | test programs | the same with clang |
| `coverage` | clang 21, llvm-profdata/llvm-cov 21 | portable | `-fprofile-instr-generate -fcoverage-mapping`, run the unit tests (`test_*`), `-g -O1` (as the sanitizer builds), `llvm-cov report -show-branch-summary` |

A finding is a compiler or clang-tidy diagnostic (`file:line: warning|error`), a cppcheck message, or a test program
that fails to build or exits non-zero (a sanitizer report makes it exit non-zero). Coverage is reported, not gated:
its row fails only when a test fails under the coverage build.

The `target-gcc` row's note is the deepest stack frame from `-fstack-usage`; a frame over 1024 bytes is a finding.

## Source lists (globs, so new files are picked up)

The configuration block at the top of `quality.sh` defines them:

- **Production** (`PROD_GLOBS`): `src/*.c src/*/*.c`: the M24SR-DISCOVERY firmware and the `src/lc/` library.
- **Test areas** (`AREAS`): one line per test directory: `name | test dir | portable source globs | target-only files`.

  | Area | Test directory | Portable sources | Left out (touches registers, or replaces libc) |
  |---|---|---|---|
  | core | `tests` (top level only) | `src/*.c`: CRC_A, NDEF, the M24SR transport | `src/i2c.c src/main.c src/system.c src/report.c src/libmin.c` |
  | lc | `tests/lc` | `src/lc/*.c`: the Last Checkpoint record and the AT parser | |

  Each `test_*.c` and `fuzz_*.c` in a test directory is one program. It links with the other `.c` files of that
  directory (helpers such as `mock_m24sr.c`) and the area's portable sources. Fuzzers build with
  `-DSTANDALONE_FUZZ` and run their full standalone iteration count. `python build.py test` uses the same rule.
- **Portable** = the union of the areas' portable sources. They must build on the host as C99.

To add a portable module: put it under an area's glob, or add a line to `AREAS` (and the same line to `test()` in
`build.py`). A new file that touches registers must be added to the area's left-out list, or its host build fails.

## Tools

| Tool | Found as | Override |
|---|---|---|
| arm-none-eabi-gcc | `PATH`; else the toolchain at `build.py`'s `DEFAULT_GCC`: in Git Bash run natively (path via `cygpath`), in WSL through interop with `GCC_EXEC_PREFIX` passed in `WSLENV` (without it the Windows gcc cannot find `cc1`). If none runs, the check is `SKIPPED (no arm-none-eabi-gcc)`, which fails the gate unless the run passes `--allow-skip target-gcc` (see above). | `ARM_GCC` |
| gcc | `gcc-11`, else `gcc` | `HOSTGCC` |
| clang, clang-tidy | `clang-21`, `clang-tidy-21` | `CLANG`, `CLANG_TIDY` |
| llvm-profdata, llvm-cov | `llvm-profdata-21`, `llvm-cov-21` | `LLVM_PROFDATA`, `LLVM_COV` |
| cppcheck | `cppcheck` (must be 2.16) | `CPPCHECK` |

Any other missing tool fails its check with "tool missing", never silently.

## Suppressions and deviations

- `tools/quality/suppress-<area>.txt`: cppcheck suppression lines, one rule in one file (`misra-c2012-11.4:src/regs.h`).
  The script concatenates every file that exists. Each line must be covered by a record in
  `docs/deviations/<area>.md`; `docs/MISRA-COMPLIANCE.md` indexes the records.
- clang-tidy: a `NOLINT(<check>)` or `NOLINTBEGIN/END(<check>)` comment that names the check and the record ID.
- Advisory MISRA rules disapplied for the whole project are listed in `DISAPPLIED` in `quality.sh` and justified in
  the Guideline Re-categorization Plan in `docs/MISRA-COMPLIANCE.md`. Nothing else may be added there.
- No blanket suppressions: no `*` file patterns, no disabled checks in `.clang-tidy`, no `#pragma` to silence a tool.
