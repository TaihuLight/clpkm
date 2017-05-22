#!/bin/bash

if [ ! "$#" -eq 2 ]; then
	echo 'Usage:'
	echo "'$0' <cl_file> <out_file>"
	exit
fi

EXE=$(realpath "$0")
DIR=$(dirname "$EXE")
OUT="${2%%.cl}"

LLVM_CONFIG=~/llvm-4.0.1-dbg/bin/llvm-config
CXX=~/llvm-4.0.1/bin/clang++
INC="-I $HOME/libclc/generic/include -include clc/clc.h"

if ! make -C "$DIR" CXX="$CXX" LLVM_CONFIG="$LLVM_CONFIG" -j 8; then
	echo 'Failed to make'
	exit
fi

export LD_LIBRARY_PATH="$HOME"/llvm-4.0.1-dbg/lib
set -x

# Check user provided file before proceed
if ! "$CXX" -fsyntax-only $INC "$1"; then
	echo '===================='
	echo 'Illegal OpenCL source'
	echo '===================='
	exit
fi

# PP
if ! "$DIR"/clpkmpp "$1" -- $INC > "$OUT"_pp.cl; then
	echo '===================='
	echo 'PP failed'
	echo '===================='
	exit
fi

if ! "$CXX" -fsyntax-only $INC "$OUT"_pp.cl; then
	echo '===================='
	echo "PP gen'd invalid output"
	echo '===================='
	exit
fi

# Inliner
if ! "$DIR"/clinliner "$OUT"_pp.cl -- $INC > "$OUT"_inline.cl ; then
	echo '===================='
	echo 'Inliner failed'
	echo '===================='
	rm -f "$TEMP"
	exit
fi

if ! "$CXX" -fsyntax-only $INC "$OUT"_inline.cl; then
	echo '===================='
	echo "inliner gen'd invalid output"
	echo '===================='
	exit
fi

# CC
if ! "$DIR"/clpkmcc "$OUT"_inline.cl -- $INC > "$OUT"_cc.cl; then
	echo '===================='
	echo 'CC failed'
	echo '===================='
	rm -f "$TEMP"
	exit
fi

if ! "$CXX" -fsyntax-only $INC "$OUT"_cc.cl; then
	echo '===================='
	echo "CC gen'd invalid output"
	echo '===================='
	exit
fi
