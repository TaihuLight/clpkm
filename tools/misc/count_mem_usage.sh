#!/usr/bin/env bash

if [ "$#" -ne 1 ]; then
	echo 'Count memory usage from CLPKM log (leglevel >= info)'
	echo 'Usage:'
	echo "  '$0' <log>"
	exit
fi


MX_USAGE=0
MX_STR='0 B'

while read LIT UNIT PRV_USAGE; do
	if [ "$PRV_USAGE" -gt "$MX_USAGE" ]; then
		MX_USAGE="$PRV_USAGE"
		MX_STR="$LIT $UNIT"
	fi
done < <(grep -e '==CLPKM== Additionally required: ' "$1" | awk -F':|\\(|\\)' '{print $2, $3}')

echo "$MX_STR $MX_USAGE"
