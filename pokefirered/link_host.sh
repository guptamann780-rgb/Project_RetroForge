#!/bin/bash
set -e
cd "$(dirname "$0")"
OBJS=$(find build_host -name '*.o')
echo "Linking $(echo "$OBJS" | wc -l) objects (expect 306)"
gcc -m32 -no-pie -o build_host/pokefirered_host $OBJS \
    $(sdl2-config --libs) -lm \
    -Wl,--defsym,gNumMusicPlayers=4 -Wl,--defsym,gMaxLines=0
echo "Linked OK:"
ls -l build_host/pokefirered_host
