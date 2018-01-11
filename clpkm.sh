#!/usr/bin/env bash

: << COMMENT
CLPKM compiler driver
====================

Introduction
--------------------
-   The source is read from stdin
-   Instrumented code will be emitted to stdout
-   Kernel profile in YAML will be emitted to stderr
-   On failure, nothing will be emitted to stdout, and log is emitted to stderr

COMMENT

function print_banner() {
  echo "-   $1:"
}

function dump_diag() {
  while read LINE; do
    # Skip empty line
    if [[ -z "$LINE" ]]; then
      echo ''
      continue
    fi
    # Don't pad banner
    if [[ -n "$(echo "$LINE" | grep '^-   .*:$')" ]]; then
      echo "$LINE"
      continue
    fi
    # Pad log
    while [[ -n "$LINE" ]]; do
      echo "    ${LINE:0:76}"
      LINE="${LINE:76}"
    done
  done < "$1" 1>&2
}

function prettify() {
  "$CLANG_FORMAT" -style=llvm "$1" > "$1"_ && \
  mv "$1"_ "$1"
}

function prettify_to {
  "$CLANG_FORMAT" -style=llvm "$1" > "$2" && \
  rm "$1"
}

# Clang tools
CLANG_ROOT="$HOME"/llvm/5.0.1/clang-rel
CLANG="$CLANG_ROOT"/bin/clang
CLANG_FORMAT="$CLANG_ROOT"/bin/clang-format
CLANG_RENAME="$CLANG_ROOT"/bin/clang-rename
CLANG_TIDY="$CLANG_ROOT"/bin/clang-tidy
export LD_LIBRARY_PATH="$CLANG_ROOT"/lib:"$LD_LIBRARY_PATH"

# Tool path configuration
CLINLINER="$HOME"/CLPKM/inliner/clinliner
RENAME_LST_GEN="$HOME"/CLPKM/rename-lst-gen/rename-lst-gen
CLPKMCC="$HOME"/CLPKM/cc/clpkmcc
TOOLKIT="$HOME"/CLPKM/toolkit.cl

# Code cache
CACHE_DIR=/tmp/clpkm-code-cache

# Temp files
TMPBASE=/tmp/clpkm_"$BASHPID"_"$RANDOM"
ORIGINAL="$TMPBASE"/original.cl
PREPROCED="$TMPBASE"/preproced.cl
INLINED="$TMPBASE"/inlined.cl
RENAME_LST="$TMPBASE"/rename.yaml
RENAMED="$TMPBASE"/renamed.cl
INSTRED="$TMPBASE"/instr.cl
PROFLIST="$TMPBASE"/profile.yaml
CCLOG="$TMPBASE"/log.txt

# Step 0
# Read source code from stdin
mkdir -p "$TMPBASE" "$CACHE_DIR"
cat > "$ORIGINAL"

print_banner 'Options' > "$CCLOG"
echo "'$@'" >> "$CCLOG"

# Step 1
# Macro expansion
print_banner 'Preprocess stage' >> "$CCLOG"

# NOTE: Preprocess in advance here may break things!
#       Please have a look at OpenCL 1.2 §6.10
"$CLANG" "$ORIGINAL" -E -P -std=cl1.2 $@ 1> "$PREPROCED" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  dump_diag "$CCLOG"
  exit 1
fi

# Lookup code cache
# Don't use two SHA-512 checksum because most filesystems have a 255-byte limit
# on filenames
# Note: chance of collision! (very rare tho)
SRC_HASH=$(sha512sum "$PREPROCED" | cut -d " " -f 1)
OPT_HASH=$(echo "$@" | sha384sum | cut -d " " -f 1)
CACHE_BASE="$CACHE_DIR"/"$SRC_HASH"-"$OPT_HASH"

if [ -f "$CACHE_BASE".cl ] && [ -f "$CACHE_BASE".yaml ]; then
  cat "$TOOLKIT" "$CACHE_BASE".cl
  cat "$CACHE_BASE".yaml 1>&2
  rm -rf "$TMPBASE"
  exit
fi

prettify "$PREPROCED" && \
"$CLANG_TIDY" "$PREPROCED" -format-style=llvm -fix \
  -checks="readability-braces-around-statements" -- -include clc/clc.h \
  -std=cl1.2 $@ 1>> "$CCLOG" 2>&1

# Failed
if [ ! "$?" -eq 0 ]; then
  dump_diag "$CCLOG"
  exit 1
fi

# Step 2
# Renaming
print_banner 'Rename stage' >> "$CCLOG"

"$RENAME_LST_GEN" "$PREPROCED" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$RENAME_LST" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  dump_diag "$CCLOG"
  exit 1
fi

# Don't do shit if it gens nothin'
if [ "$(stat -c '%s' "$RENAME_LST")" -gt 0 ]; then
  "$CLANG_RENAME" -input "$RENAME_LST" "$PREPROCED" \
    -- -include clc/clc.h -std=cl1.2 $@ \
    1> "$RENAMED" 2>> "$CCLOG"
  # Failed
  if [ ! "$?" -eq 0 ]; then
    dump_diag "$CCLOG"
    exit 1
  fi
else
  cp "$PREPROCED" "$RENAMED"
fi

# Step 3
# Inlining
print_banner 'Inline stage' >> "$CCLOG"

"$CLINLINER" "$RENAMED" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$INLINED" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  dump_diag "$CCLOG"
  exit 1
fi

# Step 4
# Invoke CLPKMCC
print_banner 'Instrument stage' >> "$CCLOG"

"$CLPKMCC" "$INLINED" \
  --source-output="$INSTRED" --profile-output="$PROFLIST" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> /dev/null 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  dump_diag "$CCLOG"
  exit 1
# Succeed
else
  prettify "$INSTRED"

  # Cache the result
  cp "$INSTRED"  "$CACHE_BASE".cl
  cp "$PROFLIST" "$CACHE_BASE".yaml

  cat "$TOOLKIT" "$INSTRED"
  cat "$PROFLIST" 1>&2
fi

# Stage 4
# Clean up
rm -rf "$TMPBASE"
