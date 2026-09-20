#!/bin/sh
set -eu

fixture_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=black:s=16x16:r=1:d=3" \
    -vf "drawbox=c=0xff0000:t=fill:enable='lt(t,1)',drawbox=c=0x00ff00:t=fill:enable='between(t,1,1.999)',drawbox=c=0x0000ff:t=fill:enable='gte(t,2)',format=yuv444p" \
    -c:v ffv1 -level 3 "$fixture_directory/three-colors.mkv"
