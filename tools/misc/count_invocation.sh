#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
	echo 'Count number of invocation from CLPKM log (leglevel >= info)'
	echo 'Usage:'
	echo "  '$0' <log>"
	exit
fi

NUM_CALL=$(grep -c 'run #1$' "$1")
NUM_RUN=$(grep -c 'run #' "$1")

printf "%s:\t$NUM_CALL -> $NUM_RUN\n" "$1"
