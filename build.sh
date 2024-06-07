#!/bin/sh

set -xe

FONT="fonts/Roboto-Regular"
[ -f "$FONT.c" ] || xxd -a -i -n font "$FONT.ttf" > "$FONT.c"

PKGS="raylib libmpdclient"
LIBS=`pkg-config --libs $PKGS`
CFLAGS=`pkg-config --cflags $PKGS`

cc $CFLAGS -o music main.c $LIBS -lm -lmpdclient -lpthread -lraylib
