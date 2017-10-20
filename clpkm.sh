#!/usr/bin/env bash

: << COMMENT
CLPKM compiler driver
====================

Introduction
--------------------
-   The source is read from stdin
-   Instrumented code will be emitted to stdout
-   Kernel profile in YAML will be emitted to stderr

COMMENT

# Tool path configuration
CLPKMCC=$(realpath ~/CLPKM/cc/clpkmcc)
CLINLINER=$(realpath ~/CLPKM/inliner/clinliner)
TOOLKIT=$(realpath ~/CLPKM/toolkit.cl)
export LD_LIBRARY_PATH=$(realpath ~/llvm-5.0.0-rev/clang-rel/lib):"$LD_LIBRARY_PATH"

# Temp files
TMPBASE=/tmp/clpkm_drv_"$BASHPID"_"$RANDOM"
ORIGINAL="$TMPBASE"_original.cl
INLINED="$TMPBASE"_inlined.cl
INSTRED="$TMPBASE"_instr.cl
PROFLIST="$TMPBASE"_profile.yaml
CCLOG="$TMPBASE".log


# Read source code from stdin
cat > "$ORIGINAL"

# : << COMMENT_OUT_TO_ENABLE_INLINER
# Invoke OpenCL inliner
"$CLINLINER" "$ORIGINAL" \
  -- -include clc/clc.h -std=cl1.2 $@ \
  1> "$INLINED" 2> "$CCLOG"

# Failed
if [ ! "$?" -eq 0 ]; then
  cat "$CCLOG" 1>&2
  exit 1
fi
# COMMENT_OUT_TO_ENABLE_INLINER

# Invoke CLPKMCC
"$CLPKMCC" "$INLINED" \
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

rm -f "$ORIGINAL" "$INLINED" "$INSTRED" "$PROFLIST" "$CCLOG"
