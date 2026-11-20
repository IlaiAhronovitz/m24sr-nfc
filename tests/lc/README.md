# tests/lc - Last Checkpoint record and AT parser (host tests, no hardware)

This directory tests `src/lc/`: a fixed-size diagnostic record for an LTE-M/NB-IoT device and a
bounded AT-response parser that fills it. `python build.py test` builds and runs everything here.

| File | What it is |
|---|---|
| `../../src/lc/record.h/.c` | The record (`lc_record_t`, 140 bytes), the cause tables, `record_encode_text()`, and `record_encode_bin()`/`record_decode_bin()`. |
| `../../src/lc/at_parse.h/.c` | `at_parse_line()` (one line) and `at_parse_feed()` (a raw byte stream). `at_parser_t` is 280 bytes and holds a 256-byte line buffer. |
| `test_lc.c` | Unit tests, exact-output text tests, binary round-trip and corruption tests, and the fixture runner. |
| `fuzz_at.c` | The fuzz target `LLVMFuzzerTestOneInput`. With `-DSTANDALONE_FUZZ` it runs 200000 deterministic iterations. |
| `fixtures/*.at` | AT transcripts. Each AT line carries a `SYNTHETIC` or `FROM-SPEC` label. |

## Design notes
- **Unknown is explicit.** Each field type has a sentinel: `0xFF`, `0xFFFF`, `0xFFFFFFFF` or `INT16_MIN`.
  Zero is always a real value, for example CE level 0, an empty FPLMN list, or a granted timer of 0 s.
  Call `lc_record_init()` first: a record zeroed with memset is **not** all-unknown.
- **No heap, no libc** in `src/lc/`. Everything compiles with `-ffreestanding`. `test()` compiles both
  files for Cortex-M3 when the Arm toolchain is present. Code size with arm-none-eabi-gcc 14.2 at `-Os`:
  record.c 8540 bytes of text, at_parse.c 4143 bytes. Neither file has .data or .bss.
- **The text summary** is ASCII (a subset of UTF-8), at most 300 characters. Segments go in priority
  order. A segment that does not fit is dropped whole, so the headline and the causes are never cut.
- **Binary layout:** `'L' 'C' | version 1 | length (2) | TLVs | CRC-16/CCITT-FALSE`. Each TLV is
  `object, resource, length, value`. Where a field overlaps, it reuses an LwM2M object/resource ID:
  3/3, 3/7, 4/2, 4/3, 4/8 to 4/13, 10/4 and 10/5. Fields that no LwM2M object carries go in extension
  object 0xF0: causes, attempts, back-off, granted timers, eDRX and FPLMN. The full layout is in a
  comment in `record.c`. A record with every field set encodes to 214 bytes (`LC_BIN_MAX` = 224).
  Unknown fields are omitted. Readers skip tags they do not know.
- **Attempts** count transitions into `+CEREG` stat 3. A repeated read during the same denial does
  not count again. A registered stat (1 or 5) resets the counter to 0, but **the last cause is kept as
  evidence**. **Back-off** is derived from the EMM cause, simplified from TS 24.301 clause 5.5.1.2.5:
  #3/#6/#7/#8 = USIM invalid; #11/#14 = forbidden PLMN; #12/#13/#15 = forbidden TA; #22 = T3346.
- **RAT:** `+CEREG` AcT 7 means only "E-UTRAN", so it is stored as LTE. `+CEDRXRDP` AcT-type 4 (WB-S1)
  refines it to LTE-M.
- **FPLMN** responses to `+CRSM`/`+CSIM` are trusted only while a read is pending. The parser marks a
  read pending when it sees the echo `AT+CRSM=176,28539,...` or when the code calls `at_parse_expect()`.
  This stops a stray `+CRSM:` answer from some other file being read as the forbidden-PLMN list.

## Sources and what is NOT verified
- AT syntax: **3GPP TS 27.007 V19.6.0 (2026-03)**, local copy, clauses 10.1.22 (+CEREG), 8.69 (+CESQ),
  7.41 (+CEDRXRDP), 7.38 (+CPSMS), 7.3 (+COPS), 8.17 (+CSIM) and 8.18 (+CRSM). The CEREG worked examples ("00C3", "00100100" = 4 min,
  "01000111" = 70 h) are used in `registered_psm.at` and in `test_timers()`.
- Murata dialect: **Type 1SC AT Command Reference Guide rev 1.1 (2024-06-25; footer dated 8/30/24)**:
  `%CEER` in clause 3.3.35, Table 3-40, p. 70; `%MEAS` in clause 3.3.1, Table 3-3, pp. 12-14.
  - **`%CEER` is UNVERIFIED in its content.** The manual gives the shape
    `%CEER: [<mode>,]<module>,<procedure>,<failure>[,<reject_cause>[,<error_info>...]]`, but it lists
    no `<module>` values. The parser assumes that a module containing "EMM" or "ESM" carries a
    TS 24.301 cause, and ignores any other module. The `%CEER` strings in the fixtures are invented.
  - The manual's list of supported 3GPP commands (p. 8) **does not include `+CESQ` or `+CRSM`**, but it
    does list `+CSIM`. The parser therefore also takes `%MEAS: Signal Quality: ...` (the only source of
    SINR) and FPLMN through `+CSIM`. Which commands the module really accepts is bench gate 3.
- **CE level:** no AT source was found, so the field is set only by `lc_record_set_ce_level()`.
  **Band:** set only by `lc_record_set_band()`, for example from Murata `%GETCFG="BAND"`. Not parsed.
- **EMM/ESM cause texts:** checked in review (2026-11-20) against **TS 24.301 V19.8.0** (ETSI TS 124 301
  V19.8.0, 2026-09), Tables 9.9.3.9.1 and 9.9.4.4.1 (values defined in Annexes A and B). Every value
  of both tables is listed, except ESM #46, which the table marks "Unused".
- **eDRX and timers:** checked against **TS 24.008 V19.5.0** (ETSI TS 124 008 V19.5.0, 2026-02):
  Table 10.5.5.32 (S1 mode, with NOTEs 4 to 6: NB-S1 codes 0000/0001 mean no eDRX, 0100/0110/0111/1000
  are read as 0010; WB-S1 codes 1110/1111 are read as 1101), and the GPRS Timer 2 / Timer 3 units of
  clauses 10.5.7.4 / 10.5.7.4a.
- **PLMN text:** an MNC whose digit count is unknown (only possible from a binary record) and is below
  100 prints with a `?`, for example `001-01?`, because it could be `01` or `001`.

## Fixtures
- `denied_15.at`: a registration-denied loop with EMM #15, three attempts, PSM requested and not granted, eDRX, FPLMN.
- `registered_psm.at`: attach with PSM granted (spec timer examples), `+COPS` numeric, `%MEAS`.
- `recovered_22.at`: congestion (#22), then roaming registration; an ESM #27 from `%CEER`; NB-IoT eDRX.
- `noise.at`: unknown, malformed and over-long (304-byte) lines, a refused SIM read, `ERROR`. The record must not change.

The fixture runner fails if any line lacks a known label. It feeds every file twice: once through the
byte stream and once line by line. It checks that both records are byte-identical, that the guard
bytes around each record are intact, and that each record survives a binary round trip.
**No real modem capture exists yet**, so every line is `SYNTHETIC`, except one assembled from TS 27.007
worked examples. Replace the fixtures with captures from bench gate 3 when they exist.

## Fuzzing
- Standalone, which runs in `build.py test`: 200000 iterations of mutated realistic lines plus random
  bytes. After that, 20000 TLV mutations with the CRC recomputed, so the decoder's TLV checks are reached.
- libFuzzer (not in CI):
  `clang -g -O1 -fsanitize=fuzzer,address,undefined tests/lc/fuzz_at.c src/lc/record.c src/lc/at_parse.c`.

## Result (2026-11-20, after review)
`python build.py test` on Windows, host gcc 5.3.0 (`-std=c99 -Wall -Wextra -Werror`), and
arm-none-eabi-gcc 14.2 (`-mcpu=cortex-m3 -Os -std=c99 -Wall -Wextra -Werror -ffreestanding`), with no
warnings:

```
44 passed, 0 failed                      (existing M24SR tests)
lc: full record = 214 bytes binary (LC_BIN_MAX 224)
lc: worst-case text = 290 chars (limit 300), longest over all <stat> = 300
lc: fixture denied_15.at          26 AT lines, 12 updates
lc: fixture registered_psm.at     16 AT lines, 6 updates
lc: fixture recovered_22.at       11 AT lines, 7 updates
lc: fixture noise.at              15 AT lines, 0 updates
lc: 522 passed, 0 failed
fuzz: 200000 iterations, 0 failures (inputs that updated the record 83086, hit a malformed line 113215,
      an over-long line 31489; valid binaries 9777)
```

Extra runs, in WSL Ubuntu 22.04, not part of `build.py`:
- gcc 11.4 with `-fsanitize=address,undefined -fno-sanitize-recover=all`: unit tests 522/522, and the
  standalone fuzz run had 0 failures.
- clang 21 libFuzzer with ASan and UBSan, empty corpus: 6,862,868 runs in 61 s, no crash.
