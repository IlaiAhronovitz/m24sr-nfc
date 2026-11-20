# Deviation records: project-wide patterns (m24sr-nfc)

Scope: every C file under `src/`. MISRA rules are cited by number only; the descriptions are our own
words. Suppressions for these records are in `tools/quality/suppress-common.txt`. Each line there
names one rule and one file, and is listed below. Nothing else is suppressed through that file.

The hardware-access patterns (registers, inline assembly, `libmin`) are in `target.md`.

---

## C-1: Rule 15.5 (Advisory): a function returns from more than one place

| | |
|---|---|
| Rule | MISRA C:2012 Rule 15.5, Advisory. |
| Where | All of `src/`: guard clauses at the top of a function (`if (bad) { return ERR; }`), and the parsers in `src/lc/`, which reject a malformed field as soon as they find it. |
| Why | The code validates untrusted input (modem text, NFC frames, a binary record) field by field. An early return per failed check keeps each check next to its error code. With one exit point, each function would need an extra result variable and nested `if` levels or flags, which makes the checks harder to review and raises cognitive complexity past the limit of 25. |
| Why it is safe | No function holds a resource that must be released on exit: there is no heap, no lock and no file. Every early return leaves the output in a defined state. The record decoder re-initialises the record on every error path, and the host tests and the fuzzer check this. |
| Control | Functions stay under 80 lines and cognitive complexity 25 (clang-tidy, enforced as an error). Every early return is covered by a host test or by the fuzzer, except defensive guards that the public API cannot reach: in `m24sr.c`, `crc_ok()` with `len < 3` (every caller passes 3 or more) and `exchange()` rejecting `apdu_len > MAX_APDU` or `data_len > M24SR_CHUNK` (callers chunk to 64); in `src/lc/record.c`, `s_ch()`'s segment-overflow flag and `edrx_cs()`'s `code > 15` guard (bounds the `s1[16]` index, CERT ARR30-C); in `src/lc/at_parse.c`, the `i >= k->n` guards in `tok_hex()`, `tok_bits()` and `tok_has()`. They are kept because they bound an index or protect against untrusted input. |
| Status | Disapplied in the Guideline Re-categorization Plan (`docs/MISRA-COMPLIANCE.md`, section 2). This record gives the reason and the controls. |
| Suppressions | None per file: `tools/quality.sh` adds `misra-c2012-15.5` once, from `DISAPPLIED`. |

## C-2: Rule 8.7 (Advisory): public API functions that only the host tests call

| | |
|---|---|
| Rule | MISRA C:2012 Rule 8.7, Advisory. |
| Where | The public functions of the portable modules, declared in their headers: `ndef.h` (`ndef_build_uri`, `ndef_build_text`), `m24sr.h` (`m24sr_write_ndef`), `crc_a.h` (`crc_a`), `src/lc/record.h` and `src/lc/at_parse.h` (the record and parser API). |
| Why | cppcheck sees only `src/`. These functions are the module interface: the host tests in `tests/` call them, and `src/lc/` is a library that the firmware image does not link yet. Making them `static` would remove the interface the tests exercise. |
| Why it is safe | Each function has exactly one declaration, in its module's header, and the header is included where it is defined (Rule 8.4 holds). |
| Control | The functions are listed here. A new external function that is not in a public header is still reported, because the suppressions are per file and only for the portable modules. |
| Suppressions | `misra-c2012-8.7:src/ndef.c`, `misra-c2012-8.7:src/m24sr.c`, `misra-c2012-8.7:src/crc_a.c`, `misra-c2012-8.7:src/lc/record.c`, `misra-c2012-8.7:src/lc/at_parse.c`. |

## C-3: Rule 2.5 (Advisory): public header macros that `src/` does not use

| | |
|---|---|
| Rule | MISRA C:2012 Rule 2.5, Advisory. |
| Where | Constants in the public headers that belong to the interface but have no user inside `src/`: `ndef.h` (TNF values), `m24sr.h` (file IDs), `src/lc/record.h` (`LC_BIN_MAX` and the error codes the callers test for), `src/lc/at_parse.h` (result codes). |
| Why | They name values of the protocol or of the API (the NDEF TNF field, the M24SR file IDs, the record's size bound) that a caller needs. The tests use them. |
| Why it is safe | They are object-like macros with a constant value; an unused one generates no code. |
| Control | Macros inside a `.c` file are not covered: an unused local macro is still reported. |
| Suppressions | `misra-c2012-2.5:src/ndef.h`, `misra-c2012-2.5:src/m24sr.h`, `misra-c2012-2.5:src/lc/record.h`, `misra-c2012-2.5:src/lc/at_parse.h`. |
