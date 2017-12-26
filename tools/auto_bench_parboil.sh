#!/bin/bash

BENCHLIST=(
	bfs
	cutcp
	# histo: ptxas error ('histo_main_kernel' uses too much shared data)
	# lbm: contains no loop
	# mri_gridding: -9999
	mri_q
	sad
	sgemm
	spmv
	# stencil: contains no loop
	# tpacf: sometime wrong result
	)

bfs=(
	NY
	UT
	1M
	SF
	)

cutcp=(
	small
	large
	)

histo=(
	default
	large
	)

lbm=(
	short
	long
	)

mri_gridding=(
	small)

mri_q=(
	small
	large
	)

sad=(
	default
	large
	)

sgemm=(
	small
	medium
	)

spmv=(
	small
	medium
	large
	)

stencil=(
	small
	default
	)

tpacf=(
	small
	medium
	large
	)

#MODE=sanity
NRUN=5

CLPKM_EXEC="env LD_LIBRARY_PATH=/usr/lib OCL_ICD_VENDORS=nvidia.icd" #amdocl64.icd
#: << DISABLE_CLPKM
CLPKM_EXEC="$CLPKM_EXEC LD_PRELOAD=$HOME/CLPKM/runtime/libclpkm.so"
CLPKM_EXEC="$CLPKM_EXEC CLPKM_PRIORITY=low CLPKM_LOGLEVEL=debug"
#DISABLE_CLPKM

OUT_DIR=./"LOG-Parboil-$(date '+%F%p%I:%M')"
mkdir -p "$OUT_DIR"

for BENCH in "${BENCHLIST[@]}"
do
	for DATA in $(eval echo "\${$BENCH[@]}")
	do
		# Bash cannot handle var name with '-'
		_BENCH=$(echo "$BENCH" | sed -r 's/_/-/g')

		OUT="$OUT_DIR"/"$_BENCH"-"$DATA"

		# mri-q names its base version as 'opencl' but 'opencl_base'
		BASE='opencl_base'
		if [ ! -e "./benchmarks/$_BENCH/src/opencl_base" ]; then
			BASE='opencl'
		fi

		# Run base version
		printf 'Running (%s, %s, base)... ' "$BENCH" "$DATA" | tee -a "$OUT_DIR"/summary.txt
		sync; sync; sync

		# Don't profile first run if it's yet built, or it will contain the time
		# to build the benchmark
		TOTAL_TIME1=$(TIMEFORMAT='%9R'; time ($CLPKM_EXEC ./parboil run "$_BENCH" "$BASE" "$DATA" \
			> "$OUT"-base.log 2>&1) 2>&1)

		if [ "$?" -eq 0 ]; then
			echo 'succeed' | tee -a "$OUT_DIR"/summary.txt
		else
			echo 'failed' | tee -a "$OUT_DIR"/summary.txt
		fi

		sync; sync; sync

		TOTAL_TIME=$(TIMEFORMAT='%9R'; time (\
			for ((RUN = 0; RUN < NRUN; ++RUN))
			do
				$CLPKM_EXEC ./parboil run "$_BENCH" "$BASE" "$DATA" \
					> "$OUT"-base.log 2>&1
			done) 2>&1)
		AVE_TIME=$(echo "($TOTAL_TIME)/ $NRUN" | bc -l)
#		AVE_TIME="$TOTAL_TIME1"
		printf "%s\t%s_(%s)\n" "$AVE_TIME" "$_BENCH" "$DATA" >> "$OUT_DIR"/time.log

		# Only run smallest data on sanity mode
		if [ "$MODE" == 'sanity' ]; then
			break
		fi

	done
done
