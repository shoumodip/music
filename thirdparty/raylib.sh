#!/bin/sh

set -e

DIR=thirdparty/raylib
URL=https://github.com/raysan5/raylib/releases/download/5.0/raylib-5.0_linux_amd64.tar.gz

mkdir -p "$DIR"

echo "[INFO] Downloading raylib"
curl -sL "$URL" | tar fzx - -C "$DIR" --strip-components 1
