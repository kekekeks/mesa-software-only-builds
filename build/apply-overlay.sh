#!/usr/bin/env bash
# Apply the softmesa overlay to a Mesa source tree.
#
# Usage: apply-overlay.sh <mesa-src-dir> <overlay-dir>
#
# Copies the combined-target files into src/gallium/targets/softmesa and
# registers the subdir in src/meson.build (idempotently). Kept separate so the
# committed Mesa submodule stays pristine.
set -euo pipefail

MESA_SRC="${1:?mesa src dir required}"
OVERLAY="${2:?overlay dir required}"

dst="$MESA_SRC/src/gallium/targets/softmesa"
rm -rf "$dst"
cp -a "$OVERLAY/gallium/targets/softmesa" "$dst"

# Windows needs the WGL target's stw init (DllMain -> stw_init) which lives in
# the wgl target's wgl.c. Copy it alongside the overlay so our combined target
# can compile it in (meson forbids referencing files across sibling subdirs).
cp "$MESA_SRC/src/gallium/targets/wgl/wgl.c" "$dst/wgl_stw_init.c"

meson_file="$MESA_SRC/src/meson.build"
if ! grep -q "gallium/targets/softmesa" "$meson_file"; then
    cat >> "$meson_file" <<'EOF'

# --- softmesa overlay: combined single-.so llvmpipe + lavapipe target ---
subdir('gallium/targets/softmesa')
EOF
fi

echo "Overlay applied to $MESA_SRC"
