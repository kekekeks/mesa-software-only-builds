#!/usr/bin/env bash
# Build the single-file software Mesa (llvmpipe + lavapipe) dylib on macOS.
#
# Runs natively on a macOS runner. Mesa has no native offscreen EGL on macOS
# (it would build the X11/apple DRI path), so we unlock the same surfaceless
# EGL/dri stack we use on Linux by:
#   * patching with_dri_platform darwin: 'apple' -> 'drm'  and adding darwin to
#     system_has_kms_drm, and
#   * providing vendored libdrm *headers* (mesa-overlay/macos-libdrm) via a fake
#     pkg-config file, with the drm *symbols* satisfied by drm_stubs.c.
# Offscreen software rendering never actually calls libdrm, so this is safe.
#
# LLVM is linked statically (Homebrew) so the dylib doesn't depend on libLLVM.
# Produces artifacts/<rid>/libsoftpipe_gl.dylib.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)
ARCH=$(uname -m)
RID="${RID:-osx-$([ "$ARCH" = arm64 ] && echo arm64 || echo x64)}"
MESA_SRC="$REPO/external/mesa"   # CI checkout is throwaway; build in place
BUILD="$REPO/.build-macos"
OUT="$REPO/artifacts/$RID"

echo "==> Applying overlay in place"
rm -rf "$BUILD"
"$REPO/build/apply-overlay.sh" "$MESA_SRC" "$REPO/mesa-overlay"

echo "==> Patching Mesa to use the drm/surfaceless dri path on darwin"
# Add darwin to the KMS/DRM system list and switch its dri platform to 'drm'.
sed -i.bak "s/'linux', 'sunos', 'android', 'managarm'/'linux', 'sunos', 'android', 'managarm', 'darwin'/" "$MESA_SRC/meson.build"
sed -i.bak "s/  with_dri_platform = 'apple'/  with_dri_platform = 'drm'/" "$MESA_SRC/meson.build"

echo "==> Fake libdrm pkg-config (vendored headers, no libs -> symbols via stubs)"
PCDIR="$BUILD/pkgconfig"
mkdir -p "$PCDIR"
cat > "$PCDIR/libdrm.pc" <<EOF
prefix=$REPO/mesa-overlay/macos-libdrm
Name: libdrm
Description: vendored libdrm headers for offscreen software Mesa
Version: 2.4.121
Cflags: -I\${prefix}
Libs:
EOF
export PKG_CONFIG_PATH="$PCDIR:${PKG_CONFIG_PATH:-}"

LLVM_PREFIX=$(brew --prefix llvm)
export PATH="$LLVM_PREFIX/bin:$PATH"
echo "==> Using llvm-config: $(command -v llvm-config) ($(llvm-config --version))"

# Force-include a small compat shim (ppoll etc.) into every TU; forcing
# HAVE_LIBDRM to unlock surfaceless EGL pulls in a few Linux-only code paths.
COMPAT="-include $REPO/mesa-overlay/macos_compat.h"
export CFLAGS="${CFLAGS:-} $COMPAT"
export CXXFLAGS="${CXXFLAGS:-} $COMPAT"

echo "==> meson setup"
meson setup "$BUILD" "$MESA_SRC" \
    --buildtype=release \
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
    -Dspirv-tools=disabled \
    -Dzstd=disabled

echo "==> ninja build softpipe_gl"
ninja -C "$BUILD" "src/gallium/targets/softpipe_gl/libsoftpipe_gl.dylib"

DYLIB="$BUILD/src/gallium/targets/softpipe_gl/libsoftpipe_gl.dylib"
echo "==> Built: $DYLIB"
file "$DYLIB" || true

mkdir -p "$OUT"
cp "$DYLIB" "$OUT/libsoftpipe_gl.dylib"
strip -x "$OUT/libsoftpipe_gl.dylib" || true

echo "==> Exported symbols (nm -gU):"
nm -gU "$OUT/libsoftpipe_gl.dylib" | grep -iE "eglGetProcAddress|vkGetInstanceProcAddr" || echo "  (none found!)"
echo "==> Dependencies (otool -L):"
otool -L "$OUT/libsoftpipe_gl.dylib"

echo "==> Done. Artifact at $OUT/libsoftpipe_gl.dylib"
