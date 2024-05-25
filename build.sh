#!/bin/sh

set -xe
xxd -a -i -n font fonts/Roboto-Regular.ttf > fonts/Roboto-Regular.c
cc `pkg-config --cflags raylib` -o music main.c `pkg-config --libs raylib` -lraylib -lm -lmpdclient -lpthread
