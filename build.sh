#!/bin/sh
cc -o music main.c -lmpdclient -lncurses -lpthread -ltag_c
