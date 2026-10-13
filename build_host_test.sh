#!/bin/sh
# Desktop sanity build: runs the real game + renderer through a software rasteriser and writes
# screenshots to out/*.ppm. Needs only gcc.   ./build_host_test.sh && ./host_test   (or ./host_test soak 5000)
mkdir -p out
gcc -O2 -fsingle-precision-constant -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation \
    -o host_test host/main_host.c host/gfx_host.c src/game.c src/render.c -lm
