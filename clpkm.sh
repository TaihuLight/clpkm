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
  echo -e '\n====================' &&
  echo " $1" &&
  echo '===================='
}

# Tool path configuration
CLPKMPP="$(realpath ~/CLPKM/pp/clpkmpp)"
CLINLINER="$(realpath ~/CLPKM/inliner/clinliner)"
RENAME_LST_GEN="$(realpath ~/CLPKM/rename-lst-gen/rename-lst-gen)"
CLPKMCC="$(realpath ~/CLPKM/cc/clpkmcc)"
TOOLKIT="$(realpath ~/CLPKM/toolkit.cl)"
export LD_LIBRARY_PATH="$(realpath ~/llvm-5.0.0-rev/clang-rel/lib)":"$LD_LIBRARY_PATH"

# Temp files
TMPBASE=/tmp/clpkm_drv_"$BASHPID"_"$RANDOM"
ORIGINAL="$TMPBASE"_original.cl
PREPROCED="$TMPBASE"_preproced.cl
INLINED="$TMPBASE"_inlined.cl
RENAME_LST="$TMPBASE"_rename.yaml
RENAMED="$TMPBASE"_renamed.cl
INSTRED="$TMPBASE"_instr.cl
PROFLIST="$TMPBASE"_profile.yaml
CCLOG="$TMPBASE".log

# Step 0
# Read source code from stdin
cat > "$ORIGINAL"

print_banner 'Options' > "$CCLOG"
echo "$@" >> "$CCLOG"

# : << COMMENT_OUT_TO_ENABLE_PREPROCESS
# Step 1
# Preprocess and invoke OpenCL inliner
print_banner 'Preprocess stage' >> "$CCLOG"

"$CLPKMPP" "$ORIGINAL" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$PREPROCED" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  cat "$CCLOG" 1>&2
  exit 1
fi

print_banner 'Inline stage' >> "$CCLOG"

"$CLINLINER" "$PREPROCED" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$INLINED" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  cat "$CCLOG" 1>&2
  exit 1
fi
# COMMENT_OUT_TO_ENABLE_PREPROCESS

# Step 2
# Rename inlined source
print_banner 'Rename stage' >> "$CCLOG"

"$RENAME_LST_GEN" "$INLINED" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$RENAME_LST" 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  cat "$CCLOG" 1>&2
  exit 1
fi

# Don't do shit if it gens nothin'
if [ "$(stat -c '%s' "$RENAME_LST")" -gt 0 ]; then
  clang-rename -input "$RENAME_LST" "$INLINED" \
    -- -include clc/clc.h -std=cl1.2 $@ \
    1> "$RENAMED" 2>> "$CCLOG"
  # Failed
  if [ ! "$?" -eq 0 ]; then
    cat "$CCLOG" 1>&2
    exit 1
  fi
fi

# Step 3
# Invoke CLPKMCC
print_banner 'Instrument stage' >> "$CCLOG"

"$CLPKMCC" "$RENAMED" \
  --source-output="$INSTRED" --profile-output="$PROFLIST" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> /dev/null 2>> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  cat "$CCLOG" 1>&2
  exit 1
# Succeed
else
  cat "$TOOLKIT" "$INSTRED"
  cat "$PROFLIST" 1>&2
fi

# Stage 4
# Clean up
rm -f "$ORIGINAL" "$INLINED" "$RENAME_LST" "$RENAMED" "$INSTRED" "$PROFLIST" \
      "$CCLOG"
