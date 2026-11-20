#!/usr/bin/env bash
# tools/quality.sh - the quality gate of docs/CODING-STANDARD.md, as one script.
#
#   bash tools/quality.sh                 every check, summary table, exit 1 on any finding
#   bash tools/quality.sh --only misra    one check (names: see CHECKS below, or --list)
#   bash tools/quality.sh --keep          keep the scratch directory (logs, objects, .su, coverage)
#   bash tools/quality.sh --allow-skip target-gcc   this SKIPPED check does not fail the run (local runs
#                                         only: refused when CI is set). Any other SKIPPED check is a FAIL.
#
# Runs in WSL (Ubuntu 22.04, any distro name), in Git Bash (target-gcc only) and on the ubuntu-24.04 CI runner.
# Nothing is written into the repository: objects, logs and profiles go to a mktemp scratch directory.
# Tool overrides (environment): HOSTGCC, CLANG, CLANG_TIDY, LLVM_PROFDATA, LLVM_COV, CPPCHECK, ARM_GCC.
# See tools/quality/README.md for what each check runs and how to add files, tests and deviations.
set -uo pipefail
shopt -s nullglob

# ---------------------------------------------------------------------------------------------------
# Repository configuration (m24sr-nfc). Everything below the next rule is the same in f769-net.
# ---------------------------------------------------------------------------------------------------
REPO_NAME="m24sr-nfc"
# clang's view of the target (the gcc target flags come from build.py's ARCH + CFLAGS).
CLANG_TARGET=(--target=arm-none-eabi -mcpu=cortex-m3 -mthumb)
# Production C: the M24SR-DISCOVERY firmware (src/*.c) and the Last Checkpoint library (src/lc/, built for
# Cortex-M but not linked into the image yet). New files are picked up.
PROD_GLOBS=(src/*.c src/*/*.c)
# Host test areas: "name | test directory | portable source globs | target-only files to leave out".
# Each test_*.c / fuzz_*.c in the directory is one program, linked with the directory's other .c files
# (helpers such as mock_m24sr.c) and the area's portable sources. Fuzzers build with -DSTANDALONE_FUZZ.
AREAS=(
  "core | tests    | src/*.c     | src/i2c.c src/main.c src/system.c src/report.c src/libmin.c"
  "lc   | tests/lc | src/lc/*.c  | "
)

# Advisory MISRA rules disapplied for the whole project. Only the Guideline Re-categorization Plan in
# docs/MISRA-COMPLIANCE.md may add one here, with its reason there.
DISAPPLIED=(15.5)

# ===================================================================================================
CHECKS=(target-gcc target-clang host-gcc host-clang misra clang-tidy sanitize-gcc sanitize-clang coverage)

WARN=(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wstrict-prototypes
      -Wmissing-prototypes -Wmissing-declarations -Wundef -Wdouble-promotion -Wformat=2 -Wnull-dereference
      -Wredundant-decls -Wvla -Wswitch-default -Werror)
GCC_ONLY=(-Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init -Wcast-align=strict)
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1)
RUN_TIMEOUT=900   # seconds per test program (the fuzzers run their full standalone count)

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT" || exit 2
exec </dev/null   # nothing here reads stdin; a stray read must not hang the gate

ONLY=""; KEEP=0; ALLOW_SKIP=()
while [ $# -gt 0 ]; do
  case "$1" in
    --only) ONLY="${2:-}"; shift 2 ;;
    --keep) KEEP=1; shift ;;
    --allow-skip) ALLOW_SKIP+=("${2:-}"); shift 2 ;;
    --list) printf '%s\n' "${CHECKS[@]}"; exit 0 ;;
    -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1 (try --help)" >&2; exit 2 ;;
  esac
done
if [ -n "$ONLY" ] && [[ " ${CHECKS[*]} " != *" $ONLY "* ]]; then
  echo "unknown check '$ONLY'; checks: ${CHECKS[*]}" >&2; exit 2
fi
for c in "${ALLOW_SKIP[@]}"; do
  if [[ " ${CHECKS[*]} " != *" $c "* ]]; then echo "--allow-skip: unknown check '$c'; checks: ${CHECKS[*]}" >&2; exit 2; fi
done
# CI must run every check: a skipped check there is a broken runner, never an accepted result.
if [ "${#ALLOW_SKIP[@]}" -gt 0 ] && [ -n "${CI:-}" ]; then
  echo "--allow-skip is refused when CI is set (CI=$CI): every check must run in CI" >&2; exit 2
fi

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/quality-${REPO_NAME}.XXXXXX")
chmod 777 "$SCRATCH"   # the Windows arm-none-eabi-gcc writes here through \\wsl.localhost when run from WSL
if [ "$KEEP" = 1 ]; then echo "scratch: $SCRATCH (kept)"; else trap 'rm -rf "$SCRATCH"' EXIT; fi

# ---- tools ----------------------------------------------------------------------------------------
pick() { for t in "$@"; do if command -v "$t" >/dev/null 2>&1; then echo "$t"; return 0; fi; done; echo ""; }
HOSTGCC=${HOSTGCC:-$(pick gcc-11 gcc)}
CLANG=${CLANG:-$(pick clang-21)}
CLANG_TIDY=${CLANG_TIDY:-$(pick clang-tidy-21)}
LLVM_PROFDATA=${LLVM_PROFDATA:-$(pick llvm-profdata-21)}
LLVM_COV=${LLVM_COV:-$(pick llvm-cov-21)}
CPPCHECK=${CPPCHECK:-$(pick cppcheck)}

# arm-none-eabi-gcc: PATH first, else the Windows toolchain named in build.py's DEFAULT_GCC: run natively
# from Git Bash (cygpath), or through interop from WSL (wslpath).
ARM_GCC=${ARM_GCC:-}
ARM_WINDOWS=0
if [ -z "$ARM_GCC" ]; then ARM_GCC=$(pick arm-none-eabi-gcc); fi
build_py_gcc_dir() {
  python3 -c 'import re,sys; s=open("build.py").read(); m=re.search(r"DEFAULT_GCC\s*=\s*r?\"([^\"]+)\"", s); print(m.group(1) if m else "")'
}
if [ -z "$ARM_GCC" ] && ! command -v wslpath >/dev/null 2>&1 && command -v cygpath >/dev/null 2>&1; then
  win_bin=$(build_py_gcc_dir)
  if [ -n "$win_bin" ] && [ -x "$(cygpath -u "$win_bin")/arm-none-eabi-gcc.exe" ]; then
    ARM_GCC="$(cygpath -u "$win_bin")/arm-none-eabi-gcc.exe"
  fi
fi
if [ -z "$ARM_GCC" ] && command -v wslpath >/dev/null 2>&1; then
  win_bin=$(build_py_gcc_dir)
  if [ -n "$win_bin" ]; then
    cand="$(wslpath -u "$win_bin")/arm-none-eabi-gcc.exe"
    if [ -x "$cand" ]; then
      ARM_GCC="$cand"; ARM_WINDOWS=1
      # A Windows gcc started from WSL cannot find cc1 from its argv[0]; point it at its own tree.
      # Only this compiler gets the variable (arm_cc): the Linux gcc must not see it.
      ARM_EXEC_PREFIX="$(wslpath -w "$(dirname "$cand")/..")\\lib\\gcc\\"
    fi
  fi
fi
arm_cc() {
  if [ "$ARM_WINDOWS" = 1 ]; then
    GCC_EXEC_PREFIX="$ARM_EXEC_PREFIX" WSLENV="${WSLENV:+$WSLENV:}GCC_EXEC_PREFIX/w" "$ARM_GCC" "$@"
  else "$ARM_GCC" "$@"; fi
}
# The compiler must actually run: a Windows .exe needs WSL interop (binfmt_misc WSLInterop), which a distro
# restart under systemd can leave unregistered.
ARM_SKIP=""
if [ -z "$ARM_GCC" ]; then
  ARM_SKIP="not on PATH, and no Windows toolchain at build.py's DEFAULT_GCC"
elif ! arm_cc --version >/dev/null 2>&1; then
  ARM_SKIP="$ARM_GCC does not run"
  [ "$ARM_WINDOWS" = 1 ] && ARM_SKIP="$ARM_SKIP (WSL interop off? /proc/sys/fs/binfmt_misc/WSLInterop is missing)"
fi
# The scratch path as the compiler sees it.
arm_out() { if [ "$ARM_WINDOWS" = 1 ]; then wslpath -w "$1"; else echo "$1"; fi; }

# clang --target=arm-none-eabi has no C library headers of its own. Give it the Arm toolchain's newlib headers
# (the ones the gcc target build uses) as system headers, so <string.h> resolves and newlib is not analysed.
CLANG_SYS=()
for inc in "${ARM_NEWLIB_INCLUDE:-}" /usr/lib/arm-none-eabi/include /usr/arm-none-eabi/include \
           "$([ -n "$ARM_GCC" ] && dirname "$ARM_GCC")/../arm-none-eabi/include"; do
  if [ -n "$inc" ] && [ -f "$inc/string.h" ]; then CLANG_SYS=(-isystem "$inc"); break; fi
done

# build.py's target flags (ARCH + CFLAGS), read from build.py itself so the two never drift.
mapfile -t TARGET_CFLAGS < <(python3 -c 'import ast,sys
src=open("build.py").read(); env={}
for node in ast.parse(src).body:
    if isinstance(node, ast.Assign) and len(node.targets)==1 and getattr(node.targets[0],"id","") in ("ARCH","CFLAGS"):
        env[node.targets[0].id]=eval(compile(ast.Expression(node.value),"build.py","eval"),{},env)
print("\n".join(env.get("CFLAGS",[])))')

# ---- source lists ---------------------------------------------------------------------------------
trim() { local s="$1"; s="${s#"${s%%[![:space:]]*}"}"; echo "${s%"${s##*[![:space:]]}"}"; }
uniq_files() { printf '%s\n' "$@" | awk 'NF && !seen[$0]++'; }

mapfile -t PROD < <(uniq_files "${PROD_GLOBS[@]}" | sort)

area_field() { trim "$(echo "$1" | cut -d'|' -f"$2")"; }
area_sources() {   # $1 = area line -> portable sources (glob minus target-only files)
  local globs excl f; globs=$(area_field "$1" 3); excl=" $(area_field "$1" 4) "
  # shellcheck disable=SC2086
  for f in $globs; do [[ "$excl" == *" $f "* ]] || echo "$f"; done | sort -u
}
area_programs() {  # $1 = area line -> test_*.c / fuzz_*.c mains
  local dir; dir=$(area_field "$1" 2); ls "$dir"/test_*.c "$dir"/fuzz_*.c 2>/dev/null | sort
}
area_helpers() {   # $1 = area line -> other .c files in the test directory
  local dir f; dir=$(area_field "$1" 2)
  for f in "$dir"/*.c; do case "$(basename "$f")" in test_*|fuzz_*) ;; *) echo "$f" ;; esac; done
}
mapfile -t PORTABLE < <(for a in "${AREAS[@]}"; do area_sources "$a"; done | sort -u)
mapfile -t TESTFILES < <(for a in "${AREAS[@]}"; do area_programs "$a"; area_helpers "$a"; done | sort -u)

# ---- reporting ------------------------------------------------------------------------------------
declare -a ROWS=()
FAILED=0
diag_lines() { tr -d '\r' < "$1" | grep -E '^[^[:space:]][^:]*:[0-9]+(:[0-9]+)?: (warning|error|fatal error):' | sort -u; }
allowed_skip() { [[ " ${ALLOW_SKIP[*]} " == *" $1 "* ]]; }
row() {  # check files findings status note
  local status="$4" note="$5"
  # A SKIPPED check proves nothing: it fails the run unless this run named it with --allow-skip.
  if [ "$status" = SKIPPED ]; then
    if allowed_skip "$1"; then note="allowed by --allow-skip: $note"
    else status=FAIL; note="SKIPPED, not allowed (no --allow-skip $1): $note"; fi
  fi
  ROWS+=("$(printf '%-15s %6s %9s  %-8s %s' "$1" "$2" "$3" "$status" "$note")")
  [ "$status" = FAIL ] && FAILED=1
  return 0
}
show() {  # print a check's findings (all of them: the log is deleted unless --keep)
  local log="$1" n; n=$(wc -l < "$2")
  if [ "$n" -gt 0 ]; then echo "--- $3: $n finding(s)"; cat "$2"; fi
  [ "$KEEP" = 1 ] && echo "    full log: $log"
  return 0
}
banner() { echo; echo "=== $1"; }
want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }
missing() { banner "$1"; echo "FAIL: $2 not found"; row "$1" "-" "-" FAIL "tool missing: $2"; }

# ---- 1. strict warnings, target code --------------------------------------------------------------
check_target_gcc() {
  banner "target-gcc: arm-none-eabi-gcc, build.py target flags + WARN + GCC_ONLY + stack usage"
  if [ -n "$ARM_SKIP" ]; then
    echo "SKIPPED (no arm-none-eabi-gcc): $ARM_SKIP"
    row target-gcc "${#PROD[@]}" "-" SKIPPED "no arm-none-eabi-gcc: $ARM_SKIP"; return
  fi
  local d="$SCRATCH/target-gcc" log="$SCRATCH/target-gcc.log" f obj fails=0
  mkdir -p "$d"; chmod 777 "$d"; : > "$log"
  for f in "${PROD[@]}"; do
    obj="$(arm_out "$d")$([ "$ARM_WINDOWS" = 1 ] && echo '\' || echo /)$(echo "$f" | tr '/' '_').o"
    arm_cc "${TARGET_CFLAGS[@]}" "${WARN[@]}" "${GCC_ONLY[@]}" -fstack-usage -Wstack-usage=1024 \
      -c "$f" -o "$obj" >> "$log" 2>&1 || { fails=$((fails+1)); echo "$f: compile failed" >> "$d/failed"; }
  done
  diag_lines "$log" > "$d/findings"
  local n; n=$(wc -l < "$d/findings")
  [ "$n" -eq 0 ] && [ "$fails" -gt 0 ] && n=$fails   # failed without a parseable diagnostic
  show "$log" "$d/findings" target-gcc
  [ "$n" -gt 0 ] && [ "$(wc -l < "$d/findings")" -eq 0 ] && { cat "$d/failed"; tail -20 "$log"; }
  local top; top=$(cat "$d"/*.su 2>/dev/null | tr -d '\r' | sort -t$'\t' -k2 -n -r | head -1 | awk -F'\t' '{print $2" B in "$1}')
  echo "deepest frame: ${top:-n/a}"
  row target-gcc "${#PROD[@]}" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" "max frame ${top%% in*}"
}

check_target_clang() {
  banner "target-clang: clang -fsyntax-only for arm-none-eabi + WARN"
  [ -z "$CLANG" ] && { missing target-clang clang-21; return; }
  local d="$SCRATCH/target-clang" log="$SCRATCH/target-clang.log" f fails=0
  mkdir -p "$d"; : > "$log"
  for f in "${PROD[@]}"; do
    "$CLANG" "${CLANG_TARGET[@]}" "${CLANG_SYS[@]}" -ffreestanding -std=c11 -Isrc "${WARN[@]}" -fsyntax-only "$f" >> "$log" 2>&1 \
      || fails=$((fails+1))
  done
  finish_compile_check target-clang "${#PROD[@]}" "$log" "$fails"
}

finish_compile_check() {  # name files log fails
  local d="$SCRATCH/$1" n
  mkdir -p "$d"; diag_lines "$3" > "$d/findings"; n=$(wc -l < "$d/findings")
  if [ "$n" -eq 0 ] && [ "$4" -gt 0 ]; then n=$4; tail -20 "$3"; fi
  show "$3" "$d/findings" "$1"
  row "$1" "$2" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" ""
}

# ---- 2. strict warnings, portable code (c99) and tests (c11) ---------------------------------------
check_host() {  # $1 = gcc|clang
  local name="host-$1" cc extra=() log="$SCRATCH/host-$1.log" f fails=0
  if [ "$1" = gcc ]; then cc=$HOSTGCC; extra=("${GCC_ONLY[@]}"); else cc=$CLANG; fi
  banner "$name: $cc, portable code -std=c99, tests -std=c11, WARN$([ "$1" = gcc ] && echo ' + GCC_ONLY')"
  [ -z "$cc" ] && { missing "$name" "$1"; return; }
  : > "$log"
  for f in "${PORTABLE[@]}"; do
    "$cc" -std=c99 -Isrc "${WARN[@]}" "${extra[@]}" -fsyntax-only "$f" >> "$log" 2>&1 || fails=$((fails+1))
  done
  for f in "${TESTFILES[@]}"; do
    local def=(); [[ "$(basename "$f")" == fuzz_* ]] && def=(-DSTANDALONE_FUZZ)
    "$cc" -std=c11 -Isrc "${def[@]}" "${WARN[@]}" "${extra[@]}" -fsyntax-only "$f" >> "$log" 2>&1 || fails=$((fails+1))
  done
  finish_compile_check "$name" "$(( ${#PORTABLE[@]} + ${#TESTFILES[@]} ))" "$log" "$fails"
}

# ---- 3. MISRA C:2012 (cppcheck) -------------------------------------------------------------------
check_misra() {
  banner "misra: cppcheck --addon=misra over the production code"
  [ -z "$CPPCHECK" ] && { missing misra cppcheck; return; }
  local d="$SCRATCH/misra" log="$SCRATCH/misra.log" supp n rc
  mkdir -p "$d/build"; supp="$d/suppressions.txt"
  local sfiles=(tools/quality/suppress-*.txt) r
  : > "$supp"
  if [ "${#sfiles[@]}" -gt 0 ]; then cat "${sfiles[@]}" | tr -d '\r' >> "$supp"; fi
  # Advisory rules disapplied project-wide (docs/MISRA-COMPLIANCE.md, Guideline Re-categorization Plan).
  for r in "${DISAPPLIED[@]}"; do echo "misra-c2012-$r" >> "$supp"; done
  echo "suppression files: ${sfiles[*]:-none} ($(grep -cvE '^\s*(#|$)' "$supp") lines, incl. disapplied: ${DISAPPLIED[*]:-none})"
  # --cppcheck-build-dir keeps the addon's .dump and ctu-file-list out of the repository.
  "$CPPCHECK" --addon=misra --std=c11 --platform=unix32 --enable=warning,style,performance,portability \
    --inline-suppr --error-exitcode=1 -Isrc --suppressions-list="$supp" --cppcheck-build-dir="$d/build" \
    --template='{file}:{line}: {severity} {id}: {message}' --quiet "${PROD[@]}" > "$log" 2>&1
  rc=$?
  tr -d '\r' < "$log" | grep -E '^[^[:space:]][^:]*:[0-9]+: [a-z]+ [A-Za-z0-9_.-]+: ' | sort -u > "$d/all"
  # "information" lines are cppcheck notices about its own analysis (e.g. normalCheckLevelMaxBranches),
  # not code findings: printed, not counted.
  grep -v ': information ' "$d/all" > "$d/findings"
  grep ': information ' "$d/all" | sed 's/^/    note (not counted): /'
  n=$(wc -l < "$d/findings")
  # --error-exitcode also fires on information notices; a non-zero exit with no output at all is a tool failure.
  if [ "$n" -eq 0 ] && [ "$rc" -ne 0 ] && [ ! -s "$d/all" ]; then n=1; echo "cppcheck failed (exit $rc):"; tail -20 "$log"; fi
  show "$log" "$d/findings" misra
  local rules; rules=$(grep -oE 'misra-c2012-[0-9]+\.[0-9]+' "$d/findings" | sort | uniq -c | sort -rn | head -8 | awk '{printf "%s %s, ", $2, $1}')
  row misra "${#PROD[@]}" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" "${rules%, }"
}

# ---- 4. CERT C and bug-finding (clang-tidy) -------------------------------------------------------
check_clang_tidy() {
  banner "clang-tidy: .clang-tidy (= the standard's -checks/-config) over the production code, target flags"
  [ -z "$CLANG_TIDY" ] && { missing clang-tidy clang-tidy-21; return; }
  local d="$SCRATCH/clang-tidy" log="$SCRATCH/clang-tidy.log" n rc
  mkdir -p "$d"
  "$CLANG_TIDY" --config-file=.clang-tidy --quiet "${PROD[@]}" -- \
    "${CLANG_TARGET[@]}" "${CLANG_SYS[@]}" -ffreestanding -std=c11 -Isrc "${WARN[@]}" > "$log" 2>&1
  rc=$?
  diag_lines "$log" > "$d/findings"; n=$(wc -l < "$d/findings")
  if [ "$n" -eq 0 ] && [ "$rc" -ne 0 ]; then n=1; echo "clang-tidy exited $rc:"; tail -20 "$log"; fi
  show "$log" "$d/findings" clang-tidy
  local checks; checks=$(grep -oE '\[[a-z0-9.,-]+\]$' "$d/findings" | tr -d '[]' | sed 's/,-warnings-as-errors//' | sort | uniq -c | sort -rn | head -6 | awk '{printf "%s %s, ", $2, $1}')
  row clang-tidy "${#PROD[@]}" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" "${checks%, }"
}

# ---- 5. build and run the host tests (sanitizers, coverage) -----------------------------------------
# build_programs <cc> <outdir> <flags...>: every area's test_*/fuzz_* program, one object per file.
# Writes "<binary>\t<area>\t<kind>" lines to <outdir>/programs; failures to <outdir>/build-failed.
build_programs() {
  local cc="$1" out="$2"; shift 2
  local flags=("$@") a prog name bin objs f std def
  mkdir -p "$out"; : > "$out/programs"; : > "$out/build-failed"
  for a in "${AREAS[@]}"; do
    for prog in $(area_programs "$a"); do
      name="$(area_field "$a" 1)_$(basename "${prog%.c}")"; bin="$out/$name"; objs=(); def=()
      [[ "$(basename "$prog")" == fuzz_* ]] && def=(-DSTANDALONE_FUZZ)
      mkdir -p "$out/obj-$name"
      for f in $(area_sources "$a") $(area_helpers "$a") "$prog"; do
        case "$f" in tests/*) std=c11 ;; *) std=c99 ;; esac
        local o; o="$out/obj-$name/$(echo "$f" | tr '/' '_').o"
        if ! "$cc" -std="$std" -Isrc "${def[@]}" "${flags[@]}" -c "$f" -o "$o" >> "$out/build.log" 2>&1; then
          echo "$name: $f" >> "$out/build-failed"; continue 2
        fi
        objs+=("$o")
      done
      if "$cc" "${flags[@]}" "${objs[@]}" -lm -o "$bin" >> "$out/build.log" 2>&1; then
        printf '%s\t%s\t%s\n' "$bin" "$(area_field "$a" 1)" "$(basename "${prog%.c}")" >> "$out/programs"
      else echo "$name: link" >> "$out/build-failed"; fi
    done
  done
}

run_programs() {  # <outdir> [filter-regex on kind]: runs each program from the repo root
  local out="$1" filt="${2:-.}" bin area kind rc
  : > "$out/run-failed"
  while IFS=$'\t' read -r bin area kind; do
    [[ "$kind" =~ $filt ]] || continue
    LLVM_PROFILE_FILE="$out/prof/$(basename "$bin")-%p.profraw" \
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:${ASAN_OPTIONS:-}" UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
      timeout "$RUN_TIMEOUT" "$bin" > "$out/run-$(basename "$bin").log" 2>&1
    rc=$?
    local last; last=$(tr -d '\r' < "$out/run-$(basename "$bin").log" | grep -v '^\s*$' | tail -1)
    if [ $rc -ne 0 ]; then
      echo "$area/$kind: exit $rc" >> "$out/run-failed"
      echo "  FAIL $area/$kind (exit $rc)"; tr -d '\r' < "$out/run-$(basename "$bin").log" | grep -E 'ERROR|runtime error|FAIL|SUMMARY' | head -15 | sed 's/^/    /'
    else echo "  ok   $area/$kind: ${last:0:110}"; fi
  done < "$out/programs"
}

check_sanitize() {  # $1 = gcc|clang
  local name="sanitize-$1" cc; [ "$1" = gcc ] && cc=$HOSTGCC || cc=$CLANG
  banner "$name: $cc ${SAN[*]}, build and run every host test and fuzzer"
  [ -z "$cc" ] && { missing "$name" "$1"; return; }
  local out="$SCRATCH/$name" n
  build_programs "$cc" "$out" "${SAN[@]}"
  [ -s "$out/build-failed" ] && { echo "build failures:"; sed 's/^/  /' "$out/build-failed"; diag_lines "$out/build.log" | head -20; }
  run_programs "$out"
  n=$(( $(wc -l < "$out/build-failed") + $(wc -l < "$out/run-failed") ))
  row "$name" "$(wc -l < "$out/programs")" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" "programs built+run clean: $(( $(wc -l < "$out/programs") - $(wc -l < "$out/run-failed") ))"
}

check_coverage() {
  banner "coverage: clang source-based coverage of the portable code, unit tests (test_*) only"
  [ -z "$CLANG" ] && { missing coverage clang-21; return; }
  [ -z "$LLVM_PROFDATA" ] || [ -z "$LLVM_COV" ] && { missing coverage "llvm-profdata-21/llvm-cov-21"; return; }
  local out="$SCRATCH/coverage" n bins=() bin area kind
  build_programs "$CLANG" "$out" -fprofile-instr-generate -fcoverage-mapping -g -O1
  run_programs "$out" '^test_'
  while IFS=$'\t' read -r bin area kind; do [[ "$kind" == test_* ]] && bins+=("$bin"); done < "$out/programs"
  n=$(( $(wc -l < "$out/build-failed") + $(wc -l < "$out/run-failed") ))
  local note="no profile"
  if [ "${#bins[@]}" -gt 0 ] && ls "$out"/prof/*.profraw >/dev/null 2>&1; then
    "$LLVM_PROFDATA" merge -sparse "$out"/prof/*.profraw -o "$out/merged.profdata"
    local objargs=("${bins[0]}"); for bin in "${bins[@]:1}"; do objargs+=(-object "$bin"); done
    "$LLVM_COV" report "${objargs[@]}" -instr-profile="$out/merged.profdata" -show-branch-summary \
      "${PORTABLE[@]}" > "$out/report.txt" 2>&1
    cat "$out/report.txt"
    note=$(awk '/^TOTAL/ {print "lines " $(NF-3) ", branches " $NF}' "$out/report.txt")
  fi
  row coverage "${#PORTABLE[@]}" "$n" "$([ "$n" -eq 0 ] && echo PASS || echo FAIL)" "${note:-see report}"
}

# ---- main -----------------------------------------------------------------------------------------
echo "quality gate: $REPO_NAME at $ROOT"
echo "tools:"
ver() { if [ -n "$2" ]; then printf '  %-15s %s\n' "$1" "$("$2" --version 2>&1 | tr -d '\r' | grep -m1 -iE 'version|[0-9]+\.[0-9]+')"; else printf '  %-15s %s\n' "$1" "NOT FOUND"; fi; }
if [ -n "$ARM_SKIP" ]; then printf '  %-15s %s\n' arm-gcc "SKIPPED: $ARM_SKIP"; else ver arm-gcc arm_cc; fi; [ "$ARM_WINDOWS" = 1 ] && echo "                  (Windows toolchain via WSL interop: $ARM_GCC)"
ver gcc "$HOSTGCC"; ver clang "$CLANG"; ver clang-tidy "$CLANG_TIDY"; ver llvm-cov "$LLVM_COV"; ver cppcheck "$CPPCHECK"
echo "production files: ${#PROD[@]}; portable files: ${#PORTABLE[@]}; test files: ${#TESTFILES[@]}"
echo "target flags (build.py): ${TARGET_CFLAGS[*]}"
echo "clang target headers: ${CLANG_SYS[*]:-none (no Arm toolchain found)}"

want target-gcc     && check_target_gcc
want target-clang   && check_target_clang
want host-gcc       && check_host gcc
want host-clang     && check_host clang
want misra          && check_misra
want clang-tidy     && check_clang_tidy
want sanitize-gcc   && check_sanitize gcc
want sanitize-clang && check_sanitize clang
want coverage       && check_coverage

echo
echo "=== summary: $REPO_NAME"
printf '%-15s %6s %9s  %-8s %s\n' check files findings status note
printf '%-15s %6s %9s  %-8s %s\n' --------------- ------ --------- -------- ----
printf '%s\n' "${ROWS[@]}"
[ "${#ALLOW_SKIP[@]}" -gt 0 ] && echo "allowed to skip (--allow-skip): ${ALLOW_SKIP[*]}"
if [ "$FAILED" = 1 ]; then echo "RESULT: FAIL"; exit 1; fi
SKIPPED_ROWS=$(printf '%s\n' "${ROWS[@]}" | awk '$4 == "SKIPPED" {print $1}' | xargs)
if [ -n "$SKIPPED_ROWS" ]; then echo "RESULT: PASS, except skipped (allowed): $SKIPPED_ROWS"; exit 0; fi
echo "RESULT: PASS"
