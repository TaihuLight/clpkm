#!/bin/bash

BENCHLIST=(
	bfs
	cutcp
	histo
	lbm
	mri_gridding
	mri_q
	sad
	sgemm
	spmv
	stencil
	tpacf
	)

declare -A SKIP_LIST=(
	['histo']='vanilla version use too much shared data'
#	['sad']='sampler_t is not supported by CLPKM atm'
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

# : << DISABLE_CLPKM
CLPKM_EXEC="env LD_PRELOAD=$HOME/CLPKM/runtime/libclpkm.so"
CLPKM_EXEC="$CLPKM_EXEC CLPKM_PRIORITY=low CLPKM_LOGLEVEL=info"
CLPKM_EXEC="$CLPKM_EXEC LD_LIBRARY_PATH=/usr/lib"
CLPKM_EXEC="$CLPKM_EXEC OCL_ICD_VENDORS=nvidia.icd"
#CLPKM_EXEC="$CLPKM_EXEC OCL_ICD_VENDORS=amdocl64.icd"
# DISABLE_CLPKM

OUT_DIR=./"LOG-Parboil-$(date '+%F%p%I:%M')"
mkdir -p "$OUT_DIR"

for BENCH in "${BENCHLIST[@]}"
do
	for DATA in $(eval echo "\${$BENCH[@]}")
	do
		# Skip
		if [[ ${SKIP_LIST[$BENCH]+F} ]]; then
			break
		fi

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

		# Don't profile first run, or it will contain the time to build the
		# benchmark
		timeout 10m $CLPKM_EXEC ./parboil run "$_BENCH" "$BASE" "$DATA" \
			> "$OUT"-base.log 2>&1

		if [ "$?" -eq 0 ]; then
			echo 'succeed' | tee -a "$OUT_DIR"/summary.txt
		else
			echo 'failed' | tee -a "$OUT_DIR"/summary.txt
		fi

		sync; sync; sync

		TOTAL_TIME=$(TIMEFORMAT='%9R'; time (\
			for ((RUN = 0; RUN < NRUN; ++RUN))
			do 
				timeout 10m $CLPKM_EXEC ./parboil run "$_BENCH" "$BASE" "$DATA" \
					> "$OUT"-base.log 2>&1
			done) 2>&1)
		AVE_TIME=$(echo "$TOTAL_TIME / $NRUN" | bc -l)
		printf "%s\t%s_(%s)\n" "$AVE_TIME" "$_BENCH" "$DATA" >> "$OUT_DIR"/time.log

		# Only run smallest data on sanity mode
		if [ "$MODE" == 'sanity' ]; then
			break
		fi

	done
done
