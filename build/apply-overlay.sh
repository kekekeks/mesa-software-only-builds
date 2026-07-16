#!/usr/bin/env bash
# Apply the softpipe_gl overlay to a Mesa source tree.
#
# Usage: apply-overlay.sh <mesa-src-dir> <overlay-dir>
#
# Copies the combined-target files into src/gallium/targets/softpipe_gl and
# registers the subdir in src/meson.build (idempotently). Kept separate so the
# committed Mesa submodule stays pristine.
set -euo pipefail

MESA_SRC="${1:?mesa src dir required}"
OVERLAY="${2:?overlay dir required}"

dst="$MESA_SRC/src/gallium/targets/softpipe_gl"
rm -rf "$dst"
cp -a "$OVERLAY/gallium/targets/softpipe_gl" "$dst"

meson_file="$MESA_SRC/src/meson.build"
if ! grep -q "gallium/targets/softpipe_gl" "$meson_file"; then
    cat >> "$meson_file" <<'EOF'

# --- softpipe_gl overlay: combined single-.so llvmpipe + lavapipe target ---
subdir('gallium/targets/softpipe_gl')
EOF
fi

echo "Overlay applied to $MESA_SRC"
