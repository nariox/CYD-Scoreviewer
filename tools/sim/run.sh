#!/bin/sh
# Render the cards screen offline into /tmp/opencode/cards_<scenario>.png
# Scenarios: games (full board), live (in-progress + upcoming), empty, offline.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build -j"$(nproc)" >/dev/null

echo "--- espn scoreboard parser (fixtures/espn_*.json) ---"
./build/cyd_sim espnparse

for s in games live empty offline connecting settings paged oldest detail detail_live detail_ot detail_sched ncaa detail_mbb_ot detail_wbb wnba detail_wnba; do
    ./build/cyd_sim "$s" "/tmp/opencode/cards_${s}.ppm" | grep -E "^lv pool" | sed "s/^/$s: /"
    magick "/tmp/opencode/cards_${s}.ppm" "/tmp/opencode/cards_${s}.png"
    echo "png: /tmp/opencode/cards_${s}.png"
done
