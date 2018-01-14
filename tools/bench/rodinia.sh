#!/bin/bash
#set -x

BENCHLIST=(
	backprop # no verify
	bfs
	# b+tree: barrier problem
	# cfd: wrong answer
	dwt2d
	# gaussian: no loop
	# heartwall: nvidia takes forever to compile
	hotspot
	hotspot3D
	# hybridsort: vanilla version out-of-resource
	kmeans
	lavaMD
	leukocyte
	lud
	# myocyte: no loop
	# nn: no loop
	nw
	# particlefilter: -9999
	pathfinder
	srad
	streamcluster
)

function print_banner() {
	echo -e '\n====================' &&
	echo " $1" &&
	echo '===================='
}

CLPKM_EXEC="env LD_LIBRARY_PATH=/usr/lib OCL_ICD_VENDORS=nvidia.icd" #amdocl64.icd
#: << DISABLE_CLPKM
CLPKM_EXEC="$CLPKM_EXEC LD_PRELOAD=$HOME/CLPKM/runtime/libclpkm.so"
CLPKM_EXEC="$CLPKM_EXEC CLPKM_PRIORITY=low CLPKM_LOGLEVEL=info"
#DISABLE_CLPKM

OUT_DIR="$PWD"/"LOG-Rodinia-$(date '+%F%p%I:%M')"
mkdir -p "$OUT_DIR"

for BENCH in "${BENCHLIST[@]}"; do
	RUN_SCRIPT=$(find "opencl/$BENCH" -name run)
	RUN_SH_DIR=$(dirname "$RUN_SCRIPT")

	OUT="$OUT_DIR"/"$BENCH"

	printf 'Running %s... ' "$BENCH" | tee -a "$OUT_DIR"/summary.txt

	# Not every benchmark got a verify file
	if [[ "$MODE" == 'sanity' && ! -e "$RUN_SH_DIR"/verify ]]; then
		#echo 'skipped' | tee -a "$OUT_DIR"/summary.txt
		#continue
		printf '<no verify> ' | tee -a "$OUT_DIR"/summary.txt
	fi

	pushd "$RUN_SH_DIR" > /dev/null 2>&1

	(
		print_banner 'Building...' && \
		make OUTPUT=Y && \
		print_banner 'Running...' && \
		timeout 1m $CLPKM_EXEC ./run > 'autobench.log' 2>&1

		if [ "$?" -ne 0 ]; then
			cat 'autobench.log'
			exit "$?"
		fi

		cat 'autobench.log'

		[[ ! -e ./verify ]] || ( \
				print_banner 'Verifying...' && \
				./verify 'autobench.log' \
		) \
	) < /dev/null > "$OUT".log 2>&1

	if [ "$?" -eq 0 ]; then
		echo 'succeed' | tee -a "$OUT_DIR"/summary.txt
	else
		echo 'failed' | tee -a "$OUT_DIR"/summary.txt
	fi

	popd > /dev/null 2>&1

done < <(find opencl -name 'run')
