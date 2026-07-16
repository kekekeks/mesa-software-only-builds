#!/usr/bin/env bash
# Runs INSIDE the Debian 12 build container. Builds the single-file
# software Mesa (llvmpipe + lavapipe) shared object and stages it.
#
# Expects the repo bind-mounted at /work. Writes the artifact to
# /work/artifacts/<rid>/.
set -euo pipefail

RID="${RID:-linux-x64}"
REPO=/work
MESA_SRC=/tmp/mesa
BUILD=/tmp/build
OUT="$REPO/artifacts/$RID"

echo "==> Preparing writable Mesa tree"
rm -rf "$MESA_SRC" "$BUILD"
cp -a "$REPO/external/mesa" "$MESA_SRC"
"$REPO/build/apply-overlay.sh" "$MESA_SRC" "$REPO/mesa-overlay"

echo "==> meson setup"
meson setup "$BUILD" "$MESA_SRC" \
    --buildtype=release \
    --prefix=/usr \
    -Db_ndebug=true \
    -Dplatforms= \
    -Degl=enabled \
    -Degl-native-platform=surfaceless \
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
    -Dvalgrind=disabled \
    -Dlibunwind=disabled \
    -Dzstd=enabled

echo "==> ninja build softpipe_gl"
ninja -C "$BUILD" src/gallium/targets/softpipe_gl/libsoftpipe_gl.so

SO="$BUILD/src/gallium/targets/softpipe_gl/libsoftpipe_gl.so"
echo "==> Built: $SO"
file "$SO"

mkdir -p "$OUT"
cp "$SO" "$OUT/libsoftpipe_gl.so"
# Strip to shrink; keep it a plain shared object.
strip --strip-unneeded "$OUT/libsoftpipe_gl.so"

echo "==> Dropping DT_NEEDED entries that contribute no symbols"
"$REPO/build/strip-unused-needed.sh" "$OUT/libsoftpipe_gl.so"

echo "==> Verification (see verify-binary.sh for the enforced checks)"
"$REPO/build/verify-binary.sh" "$OUT/libsoftpipe_gl.so"

echo "==> Done. Artifact at $OUT/libsoftpipe_gl.so"
