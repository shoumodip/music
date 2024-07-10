#!/bin/sh

set -e

echo "[INFO] Generating font header"
xxd -a -i -n font fonts/Roboto-Regular.ttf > fonts/Roboto-Regular.c

[ -d thirdparty/raylib ] || ./thirdparty/raylib.sh
[ -d thirdparty/libmpdclient ] || ./thirdparty/libmpdclient.sh

echo "[INFO] Building program"
cc \
    -Ithirdparty/raylib/include -Ithirdparty/libmpdclient/include \
    -o music main.c \
    -Lthirdparty/raylib/lib -l:libraylib.a \
    -Lthirdparty/libmpdclient/lib -l:libmpdclient.a \
    -lm
