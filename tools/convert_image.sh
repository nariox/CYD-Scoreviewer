#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
INPUT="$DIR/basketball.png"
OUTPUT_DIR="$DIR/../main"
OUTPUT_NAME="basketball_img"

if [ ! -f "$INPUT" ]; then
    echo "Error: $INPUT not found"
    exit 1
fi

if ! python3 -c "from lv_img_conv import convert" 2>/dev/null; then
    echo "Installing lv-img-conv..."
    python3 -m venv "$DIR/.venv"
    "$DIR/.venv/bin/pip" install lv-img-conv -q
    CONV="$DIR/.venv/bin/python3 -m lv_img_conv"
else
    CONV="python3 -m lv_img_conv"
fi

echo "Converting $INPUT -> $OUTPUT_DIR/${OUTPUT_NAME}.c/.h"
$CONV convert "$INPUT" \
    --format RGB565A8 \
    --output-dir "$OUTPUT_DIR" \
    --output-file "$OUTPUT_NAME" \
    --cpp

echo "Done"
