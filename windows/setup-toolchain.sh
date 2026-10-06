#!/usr/bin/env bash
# Build environment for the Windows port, inside MSYS2's CLANG64 environment (windows/setup.cmd
# downloads MSYS2 into .toolchain/msys64 and runs this script there). Installs the compiler and
# the libraries of shell.nix; the ones MSYS2 does not package or ships without CMake files
# (magic_enum, xbyak, miniz, VulkanMemoryAllocator) are built from source into the same prefix. Everything stays in
# .toolchain/: nothing is installed system-wide.
set -euo pipefail
if [[ ${MSYSTEM:-} != CLANG64 ]]; then echo 'Run inside the CLANG64 environment (windows/setup.cmd).' >&2; exit 1; fi
cd -- "$(dirname -- "$0")/.."
P=mingw-w64-clang-x86_64
pacman -S --noconfirm --needed git make patch $P-toolchain $P-cmake $P-ninja $P-pkgconf $P-python \
    $P-sdl3 $P-vulkan-headers $P-vulkan-loader $P-boost $P-fmt $P-robin-map $P-xxhash \
    $P-glslang $P-spirv-cross $P-spirv-tools $P-spirv-headers $P-zydis $P-ffmpeg $P-dlfcn \
    $P-gtk4 $P-libadwaita $P-python-gobject
src=.toolchain/src
mkdir -p "$src"
fetch() { # name url tag
    if [[ ! -d $src/$1 ]]; then git clone -q --depth 1 --branch "$3" "$2" "$src/$1"; fi
}
fetch magic_enum https://github.com/Neargye/magic_enum.git v0.9.7
fetch xbyak https://github.com/herumi/xbyak.git v7.30
fetch miniz https://github.com/richgel999/miniz.git 3.1.0
fetch VulkanMemoryAllocator https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git v3.3.0
for name in magic_enum xbyak miniz VulkanMemoryAllocator; do
    if [[ -f $src/$name.installed ]]; then continue; fi
    extra=()
    case $name in
        magic_enum) extra=(-DMAGIC_ENUM_OPT_BUILD_EXAMPLES=OFF -DMAGIC_ENUM_OPT_BUILD_TESTS=OFF) ;;
        miniz) extra=(-DBUILD_EXAMPLES=OFF -DBUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF -DBUILD_FUZZERS=OFF) ;;
    esac
    cmake -S "$src/$name" -B "$src/$name/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$MINGW_PREFIX" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "${extra[@]}" > /dev/null
    cmake --build "$src/$name/build" > /dev/null
    cmake --install "$src/$name/build" > /dev/null
    touch "$src/$name.installed"
    echo "Installed $name into $MINGW_PREFIX"
done
git submodule update --init --recursive
echo 'Toolchain ready.'
