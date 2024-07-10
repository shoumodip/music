#!/bin/sh

set -e

DIR=thirdparty/libmpdclient
URL=https://github.com/MusicPlayerDaemon/libmpdclient/archive/refs/tags/v2.22.tar.gz

mkdir -p "$DIR"
cd "$DIR"

echo "[INFO] Downloading libmpdclient"
curl -sL "$URL" | tar fzx - --strip-components 1

cat << EOF > include/config.h
#pragma once

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 6600
#define DEFAULT_SOCKET "/run/mpd/socket"
#define ENABLE_TCP
#define HAVE_GETADDRINFO
#define HAVE_STRNDUP
#define PACKAGE "libmpdclient"
#define VERSION "2.22"
EOF

sed "s/@MAJOR_VERSION@/2/;s/@MINOR_VERSION@/22/;s/@PATCH_VERSION@/0/" include/mpd/version.h.in > include/mpd/version.h

echo "[INFO] Building libmpdclient"
cc -Iinclude -c src/*.c

mkdir lib
ar rcs lib/libmpdclient.a *.o

rm *.o
