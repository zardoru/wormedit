# Ubuntu 22.04: glibc 2.35. Keep the digest when refreshing the tag.
FROM ubuntu:22.04@sha256:0a9887931b0e38af2e3487ed39e29436b0535c4f3f52e57745baaf79d1898956
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git gpg-agent software-properties-common python3 ruby-asciidoctor \
    build-essential pkg-config patchelf file desktop-file-utils zlib1g-dev \
    libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
    wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev libudev-dev \
    libdbus-1-dev libibus-1.0-dev libvulkan-dev \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update && apt-get install -y --no-install-recommends g++-13 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-13 CXX=g++-13
WORKDIR /opt/tools
# Fixed release URLs, never continuous/latest. Verify before executing or extracting.
RUN curl -fL --retry 3 https://github.com/Kitware/CMake/releases/download/v4.2.3/cmake-4.2.3-linux-x86_64.tar.gz -o cmake.tar.gz \
    && echo '5bb505d5e0cca0480a330f7f27ccf52c2b8b5214c5bba97df08899f5ef650c23  cmake.tar.gz' | sha256sum -c - \
    && tar -xf cmake.tar.gz --strip-components=1 -C /opt && rm cmake.tar.gz
RUN curl -fL --retry 3 https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage -o linuxdeploy.AppImage \
    && echo '4648f278ab3ef31f819e67c30d50f462640e5365a77637d7e6f2ad9fd0b4522a  linuxdeploy.AppImage' | sha256sum -c - \
    && chmod +x linuxdeploy.AppImage && ./linuxdeploy.AppImage --appimage-extract \
    && mv squashfs-root linuxdeploy && rm linuxdeploy.AppImage
RUN curl -fL --retry 3 https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage -o appimagetool.AppImage \
    && echo 'ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  appimagetool.AppImage' | sha256sum -c - \
    && chmod +x appimagetool.AppImage && ./appimagetool.AppImage --appimage-extract \
    && mv squashfs-root appimagetool && rm appimagetool.AppImage
RUN curl -fL --retry 3 https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 -o runtime-x86_64 \
    && echo '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d  runtime-x86_64' | sha256sum -c -
COPY build-lame-sdk.sh /opt/tools/build-lame-sdk.sh
RUN bash /opt/tools/build-lame-sdk.sh /opt/lame-build /opt/ffmpeg 3
# A small LGPL FFmpeg with the shared LGPL LAME MP3 encoder.
RUN curl -fL --retry 3 https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz -o ffmpeg-9.0.2.tar.xz \
    && echo '8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e  ffmpeg-9.0.2.tar.xz' | sha256sum -c - \
    && tar -xf ffmpeg-9.0.2.tar.xz \
    && cd ffmpeg-9.0.2 \
    && export LD_LIBRARY_PATH=/opt/ffmpeg/lib \
    && ./configure --prefix=/opt/ffmpeg --enable-shared --disable-static \
       --disable-programs --disable-doc --disable-debug --disable-autodetect \
       --disable-network --disable-avdevice --disable-avfilter --disable-encoders \
       --enable-libmp3lame --enable-encoder=libmp3lame --disable-muxers --enable-muxer=mp3 \
       --extra-cflags=-I/opt/ffmpeg/include --extra-ldflags=-L/opt/ffmpeg/lib --disable-x86asm \
    && make -j3 && make install \
    && mkdir -p /opt/ffmpeg/share/wormedit-ffmpeg \
    && cp COPYING.LGPLv2.1 LICENSE.md config.h ffbuild/config.log /opt/ffmpeg/share/wormedit-ffmpeg/ \
    && cp ../ffmpeg-9.0.2.tar.xz /opt/ffmpeg/share/wormedit-ffmpeg/ \
    && cd .. && rm -rf ffmpeg-9.0.2 ffmpeg-9.0.2.tar.xz
COPY linux/runtime-downloads.txt linux/fetch-runtime-notices.sh /opt/tools/
RUN bash /opt/tools/fetch-runtime-notices.sh
# std::bind_back needs GCC 14; keep FFmpeg's cached GCC 13 build above.
RUN apt-get update && apt-get install -y --no-install-recommends g++-14 \
    && rm -rf /var/lib/apt/lists/*
ENV CC=gcc-14 CXX=g++-14
# Ubuntu separates the CLI from the Ruby library package installed above.
RUN apt-get update && apt-get install -y --no-install-recommends asciidoctor \
    && rm -rf /var/lib/apt/lists/*
ENV PATH=/opt/bin:$PATH PKG_CONFIG_PATH=/opt/ffmpeg/lib/pkgconfig LD_LIBRARY_PATH=/opt/ffmpeg/lib
WORKDIR /work
ENTRYPOINT ["bash", "/source/packaging/linux/build-in-container.sh"]
