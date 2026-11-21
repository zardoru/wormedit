#!/bin/sh
set -eu

fixture_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
colors="drawbox=c=0xff0000:t=fill:enable='lt(t,1)',drawbox=c=0x00ff00:t=fill:enable='between(t,1,1.999)',drawbox=c=0x0000ff:t=fill:enable='gte(t,2)'"
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=black:s=16x16:r=1:d=3" \
    -vf "$colors,format=yuv444p" \
    -c:v ffv1 -level 3 "$fixture_directory/three-colors.mkv"
# MPEG-4 with B-frames in AVI: the first decoded frame is timed one tick after the stream start.
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=black:s=16x16:r=25:d=3" \
    -vf "$colors" -c:v mpeg4 -bf 2 "$fixture_directory/three-colors-bframes.avi"
# MPEG-1 program stream with a non-zero start time; seeded noise gives it enough packets that
# FFmpeg's timestamp seek lands after the requested time.
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i "color=c=black:s=16x16:r=30:d=3" \
    -vf "$colors,noise=alls=40:allf=t:all_seed=1" \
    -c:v mpeg1video -bf 2 -g 15 -q:v 2 -f mpeg -packetsize 2048 \
    "$fixture_directory/three-colors-late-seek.mpg"
