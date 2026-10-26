#!/usr/bin/env bash
# These dependencies ship inside the pinned, static AppImage runtime.
set -euo pipefail
mkdir -p /opt/runtime-notices
cd /opt/runtime-notices
while read -r checksum name url; do
    curl -fL --retry 3 "$url" -o "$name"
    printf '%s  %s\n' "$checksum" "$name" | sha256sum -c -
done < /opt/tools/runtime-downloads.txt
tar -xOf type2-runtime-20251108.tar.gz type2-runtime-20251108/LICENSE > runtime-LICENSE
tar -xOf fuse-3.15.0.tar.xz fuse-3.15.0/LGPL2.txt > libfuse-LGPL2.txt
tar -xOf squashfuse-0.5.2.tar.gz squashfuse-0.5.2/LICENSE > squashfuse-LICENSE
cat > README.txt <<'TEXT'
AppImage type-2 runtime 20251108 (MIT), and embedded dependency notices.
The included runtime archive contains BUILD.md, Makefile, dependency build
scripts and the libfuse patch. The corresponding libfuse and squashfuse source
archives are included to allow rebuilding/relinking with modified libraries.
Extract type2-runtime-20251108.tar.gz, then see BUILD.md and
scripts/docker/build-with-docker.sh. Limit the upstream make/ninja job counts
to three when rebuilding on a shared machine.

The runtime's upstream build uses Alpine 3.21, musl, libfuse 3.15.0,
squashfuse 0.5.2, zstd, zlib and mimalloc. The permissive dependency license
texts are preserved here; source URLs and SHA-256 values are in downloads.txt.
Wormedit's custom AppRun is a shell launcher under the project Unlicense.
TEXT
cp /opt/tools/runtime-downloads.txt downloads.txt
