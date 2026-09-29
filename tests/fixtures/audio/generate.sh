#!/bin/sh
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

encode_pair() {
    name=$1
    source=$2

    ffmpeg -hide_banner -loglevel error -y \
        -f lavfi -i "$source" -map_metadata -1 -vn -ar 44100 -ac 2 \
        -c:a vorbis -strict -2 -q:a 2 "$fixture_dir/$name.ogg"
    ffmpeg -hide_banner -loglevel error -y \
        -f lavfi -i "$source" -map_metadata -1 -vn -ar 44100 -ac 2 \
        -c:a libmp3lame -b:a 64k -write_xing 0 -id3v2_version 0 \
        "$fixture_dir/$name.mp3"
}

encode_pair keysound-c4 \
    'sine=frequency=261.63:sample_rate=44100:duration=0.22,afade=t=in:st=0:d=0.005,afade=t=out:st=0.12:d=0.10'
encode_pair keysound-e4 \
    'sine=frequency=329.63:sample_rate=44100:duration=0.22,afade=t=in:st=0:d=0.005,afade=t=out:st=0.12:d=0.10'
encode_pair keysound-g4 \
    'sine=frequency=392.00:sample_rate=44100:duration=0.22,afade=t=in:st=0:d=0.005,afade=t=out:st=0.12:d=0.10'
encode_pair polyphonic-chord \
    'sine=frequency=261.63:sample_rate=44100:duration=0.50[a];sine=frequency=329.63:sample_rate=44100:duration=0.50[b];sine=frequency=392.00:sample_rate=44100:duration=0.50[c];[a][b][c]amix=inputs=3:duration=longest:normalize=0,afade=t=in:st=0:d=0.005,afade=t=out:st=0.35:d=0.15'
encode_pair sliced-seek-source \
    'aevalsrc=0.25*sin(2*PI*if(lt(t\,2)\,220\,if(lt(t\,4)\,277\,if(lt(t\,6)\,330\,if(lt(t\,8)\,392\,if(lt(t\,10)\,440\,523)))))*t)*lt(mod(t\,2)\,0.4):s=44100:d=12'
encode_pair silence 'anullsrc=r=44100:cl=mono:d=1.0'

# Short Unicode metadata examples for the new-project audio import flow.
ffmpeg -hide_banner -loglevel error -y \
    -f lavfi -i 'sine=frequency=261.63:sample_rate=44100:duration=0.22' \
    -ar 44100 -ac 2 -c:a vorbis -strict -2 -q:a 2 \
    -metadata title='Título 雪' -metadata artist='Björk' \
    "$fixture_dir/tagged-project.ogg"
ffmpeg -hide_banner -loglevel error -y \
    -i "$fixture_dir/keysound-c4.mp3" -c:a copy -id3v2_version 4 \
    -metadata title='Título 雪' -metadata artist='Björk' \
    "$fixture_dir/tagged-project.mp3"
