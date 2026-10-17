#!/bin/sh
# Render the cards screen offline into /tmp/opencode/cards_<scenario>.png
# Scenarios: games (full board), live (in-progress + upcoming), empty, offline.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j"$(nproc)" >/dev/null
for s in games live empty offline paged oldest; do
    ./build/cyd_sim "$s" "/tmp/opencode/cards_${s}.ppm" >/dev/null
    magick "/tmp/opencode/cards_${s}.ppm" "/tmp/opencode/cards_${s}.png"
    echo "png: /tmp/opencode/cards_${s}.png"
done
