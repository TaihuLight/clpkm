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

function workload() {
	trap "kill -- -$(ps -o pgid= $BASHPID | grep -o [0-9]*)" EXIT
	while true; do
		# Built-in $RANDOM has poor quality...
		PICK_BENCH=$(($(od -vAn -N4 -tu4 < /dev/urandom) % ${#BENCHLIST[@]}))
		PICK_BENCH="${BENCHLIST[$PICK_BENCH]}"
		LAST_DATA=$(eval echo "\$\(\(\${#$PICK_BENCH[@]} - 1\)\)")
		PICK_DATA=$(eval echo "\${$PICK_BENCH[$LAST_DATA]}")

		PICK_BENCH=$(echo "$PICK_BENCH" | sed -r 's/_/-/g')
		BASE='opencl_base'
		if [ ! -e "./benchmarks/$PICK_BENCH/src/opencl_base" ]; then
			BASE='opencl'
		fi
		#echo "$PICK_BENCH" "$BASE" "$PICK_DATA"
		$CLPKM_EXEC ./parboil run "$PICK_BENCH" "$BASE" "$PICK_DATA" --no-check #> /dev/null 2>&1

#		$CLPKM_EXEC ./parboil run bfs opencl_base SF --no-check
	done
	}

#MODE=sanity
NRUN=5

CLPKM_EXEC="env LD_LIBRARY_PATH=/usr/lib OCL_ICD_VENDORS=nvidia.icd" #amdocl64.icd
#: << DISABLE_CLPKM
CLPKM_EXEC="$CLPKM_EXEC LD_PRELOAD=$HOME/CLPKM/runtime/libclpkm.so"
CLPKM_EXEC="$CLPKM_EXEC CLPKM_PRIORITY=low CLPKM_LOGLEVEL=debug"
#DISABLE_CLPKM

if [ "$#" -ge 1 ]; then
	if ! [[ "$1" =~ ^[0-9]+$ ]]; then
		echo "'$1' is not a number"
		exit 1
	fi
	trap "kill -- -$(ps -o pgid= $BASHPID | grep -o [0-9]*)" EXIT
	for ((i = 0; i < "$1"; i++)); do
		workload &
		sleep 1
	done
	wait
	exit
fi

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
