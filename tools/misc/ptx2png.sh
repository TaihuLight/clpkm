#!/bin/bash

if [ ! "$#" -eq 2 ]; then
	echo 'Generate control flow graph PNG from PTX file (via clGetProgramInfo)'
	echo 'Usage:'
	echo "  '$0' <input-ptx.s> <output.png>"
	exit 1
fi

TMPVER=/tmp/ptx2png-"$BASHPID"-"$RANDOM".s
TMPBIN=/tmp/ptx2png-"$BASHPID"-"$RANDOM".cubin
TMPDOT=/tmp/ptx2png-"$BASHPID"-"$RANDOM".dot

# Workaround old CUDA
cp "$1" "$TMPVER"
sed -i 's/\.version 6\.1/\.version 5\.0/g' "$TMPVER"

# Compile to cubin
/opt/cuda/bin/ptxas -arch=sm_35 "$TMPVER" -o "$TMPBIN"

# Generate CFG
/opt/cuda/bin/nvdisasm -cfg "$TMPBIN" > "$TMPDOT"
sed -i 's/fontname=\"Courier\",fontsize=10/fontname=\"Monaco\",fontsize=18/g' "$TMPDOT"
dot -o"$2" -Tpng < "$TMPDOT"

# Clean up
rm "$TMPVER" "$TMPBIN" "$TMPDOT"
