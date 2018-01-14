#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
	echo 'Count number of invocation from CLPKM log (leglevel >= info)'
	echo 'Usage:'
	echo "  '$0' <log>"
	exit
fi

NUM_CALL=$(grep 'run #1$' "$1" | wc -l)
NUM_RUN=$(grep 'run #' "$1" | wc -l)

printf "%s:\t$NUM_CALL -> $NUM_RUN\n" "$1"
