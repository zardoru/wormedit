#!/usr/bin/env bash
set -euo pipefail

# Prepare a fresh Ubuntu 24.04 cloud container (Claude Code on the web) so a session can
# build the frontend and run the headless ctest suites, event harness included.
# It mirrors the linux-gcc job of .github/workflows/ci.yml.
#
#   scripts/setup-cloud-env.sh            install tools and dependency sources (idempotent)
#   scripts/setup-cloud-env.sh test       setup + configure + build + ctest (event-harness)
#   scripts/setup-cloud-env.sh env        print the exports; use: eval "$(scripts/setup-cloud-env.sh env)"
#   scripts/setup-cloud-env.sh app        also install the X11/Xvfb/GL packages the app and
#                                         scripts/smoke-compose-virtual-x.sh need
#
# Why each step exists in this environment:
#   * g++-14: the repo needs C++23 std::expected/std::bind_back; Ubuntu 24.04 defaults to GCC 13.
#   * CMake 4.2 from PyPI: libotoworm requires it, apt has 3.28.
#   * libogg/libvorbis from GitHub tags: downloads.xiph.org answers 403 through the egress proxy,
#     so FetchContent cannot use the tarballs. CMake's WORMEDIT_OGG_SOURCE_DIR and
#     WORMEDIT_VORBIS_SOURCE_DIR point at the same sources (same versions) instead.
#   * SDL3, Nuklear, Catch2, Lua, zlib, ... come from github.com and fetch normally.
# Events-only SDL (no X11/Wayland, SDL_VIDEODRIVER never initialized) is what the harness uses,
# so no display, GPU or audio device is needed.

cmd=${1:-setup}
root_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tools_dir=${WORMEDIT_CLOUD_TOOLS:-$HOME/.cache/wormedit-cloud/tools}
deps_dir=${WORMEDIT_CLOUD_DEPS:-$HOME/.cache/wormedit-cloud/deps}
ogg_dir=$deps_dir/libogg
vorbis_dir=$deps_dir/libvorbis
preset=${WORMEDIT_CLOUD_PRESET:-headless-events}

print_env() {
    echo "export PATH=\"$tools_dir/bin:\$PATH\""
    echo "export CC=gcc-14 CXX=g++-14"
    echo "export WORMEDIT_CLOUD_CMAKE_ARGS=\"-DWORMEDIT_OGG_SOURCE_DIR=$ogg_dir -DWORMEDIT_VORBIS_SOURCE_DIR=$vorbis_dir\""
}

if [[ $cmd == env ]]; then
    print_env
    exit 0
fi

sudo_cmd=()
if [[ $(id -u) -ne 0 ]]; then sudo_cmd=(sudo); fi

packages=(g++-14 ninja-build ccache lld zlib1g-dev python3-venv git)
if [[ $cmd == app ]]; then
    packages+=(xvfb xauth xdotool x11-utils imagemagick libx11-dev libxext-dev libxcursor-dev
        libxi-dev libxrandr-dev libxss-dev libxtst-dev libxfixes-dev libxrender-dev libwayland-dev wayland-protocols libdrm-dev libgbm-dev libudev-dev libdbus-1-dev libxkbcommon-dev libasound2-dev libgl1-mesa-dri
        mesa-vulkan-drivers libvulkan-dev pkg-config)
fi
"${sudo_cmd[@]}" apt-get update -qq
"${sudo_cmd[@]}" apt-get install -y -qq "${packages[@]}"

if [[ ! -x $tools_dir/bin/cmake ]] || ! "$tools_dir/bin/cmake" --version | grep -q "4\.2\."; then
    python3 -m venv "$tools_dir"
    "$tools_dir/bin/pip" install -q cmake==4.2.3
fi

if [[ $cmd == app ]]; then
    "$tools_dir/bin/pip" install -q pillow   # scripts/compare-ui-captures.py, render snapshots
fi

mkdir -p "$deps_dir"
if [[ ! -f $ogg_dir/CMakeLists.txt ]]; then
    git clone -q --depth 1 --branch v1.3.6 https://github.com/xiph/ogg.git "$ogg_dir"
fi
if [[ ! -f $vorbis_dir/CMakeLists.txt ]]; then
    git clone -q --depth 1 --branch v1.3.7 https://github.com/xiph/vorbis.git "$vorbis_dir"
fi
git -C "$root_dir" submodule update --init --recursive

eval "$(print_env)"
echo "Environment ready. In a new shell run: eval \"\$($0 env)\""

if [[ $cmd == test ]]; then
    cd "$root_dir"
    # shellcheck disable=SC2086
    cmake --preset "$preset" $WORMEDIT_CLOUD_CMAKE_ARGS
    cmake --build --preset "$preset" --parallel "$(nproc)" -- -k 0
    ctest --preset "$preset" --output-on-failure --timeout 120
fi
