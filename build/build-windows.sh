#!/usr/bin/env bash
# Build the single-file software Mesa (llvmpipe + lavapipe) DLL on Windows.
#
# Runs in an MSYS2 MINGW64 shell (see .github/workflows/build.yml). MinGW ships
# a static LLVM and lets us statically link the C/C++ runtime, so the resulting
# softpipe_gl.dll depends only on system DLLs (kernel32, user32, ...).
#
# Produces artifacts/win-x64/softpipe_gl.dll.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)
RID="${RID:-win-x64}"
# Mesa's tree contains symlinks that Windows can't recreate, so we don't copy
# it; the CI checkout is throwaway, so we apply the overlay in place.
MESA_SRC="$REPO/external/mesa"
BUILD="$REPO/.build-win"
OUT="$REPO/artifacts/$RID"

echo "==> Applying overlay in place"
rm -rf "$BUILD"
"$REPO/build/apply-overlay.sh" "$MESA_SRC" "$REPO/mesa-overlay"

# Some MinGW LLVM packages' `llvm-config --link-static` lists -lPolly with no
# static archive. Provide empty stubs if they are missing (same as Debian).
llvmlib=$(llvm-config --libdir)
for p in Polly PollyISL; do
    if ! [ -e "$llvmlib/lib$p.a" ]; then
        echo "==> Creating empty stub lib$p.a"
        ar rcs "$llvmlib/lib$p.a" || true
    fi
done

# MSYS2's llvm-config emits `-lzstd.dll` (the import lib) in --system-libs, which
# forces a libzstd.dll runtime dependency that -static can't override. Overwrite
# the import lib with the STATIC archive so `-lzstd.dll` pulls static objects.
for d in "$llvmlib" /mingw64/lib; do
    if [ -e "$d/libzstd.a" ] && [ -e "$d/libzstd.dll.a" ]; then
        cp -f "$d/libzstd.a" "$d/libzstd.dll.a"
    fi
done

echo "==> meson setup"
meson setup "$BUILD" "$MESA_SRC" \
    --buildtype=release \
    -Db_ndebug=true \
    -Dplatforms=windows \
    -Degl=enabled \
    -Degl-native-platform=windows \
    -Dglvnd=disabled \
    -Dglx=disabled \
    -Dgbm=disabled \
    -Dopengl=true \
    -Dgles1=disabled \
    -Dgles2=enabled \
    -Dshared-glapi=enabled \
    -Dllvm=enabled \
    -Dshared-llvm=disabled \
    -Dllvm-orcjit=false \
    -Dgallium-drivers=llvmpipe \
    -Dvulkan-drivers=swrast \
    -Dvideo-codecs= \
    -Dgallium-va=disabled \
    -Dgallium-extra-hud=false \
    -Dvulkan-layers= \
    -Dtools= \
    -Dbuild-tests=false \
    -Dspirv-tools=disabled \
    -Dzstd=disabled

echo "==> ninja build softpipe_gl"
ninja -C "$BUILD" "src/gallium/targets/softpipe_gl/softpipe_gl.dll"

DLL="$BUILD/src/gallium/targets/softpipe_gl/softpipe_gl.dll"
echo "==> Built: $DLL"
file "$DLL" || true

mkdir -p "$OUT"
cp "$DLL" "$OUT/softpipe_gl.dll"
strip --strip-unneeded "$OUT/softpipe_gl.dll" || true

echo "==> Exported symbols:"
objdump -p "$OUT/softpipe_gl.dll" | grep -A50 "Export Address Table" | grep -iE "eglGetProcAddress|vkGetInstanceProcAddr" || {
    echo "WARNING: expected exports not found"; }

echo "==> DLL dependencies:"
objdump -p "$OUT/softpipe_gl.dll" | grep -i "DLL Name" | sort -u

echo "==> Done. Artifact at $OUT/softpipe_gl.dll"
