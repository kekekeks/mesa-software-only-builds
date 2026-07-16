#!/usr/bin/env bash
# Host entry point: build the Debian 12 image and run the containerised build
# of the single-file software Mesa (llvmpipe + lavapipe) .so.
#
# Usage: build/build-linux.sh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/.." && pwd)
RID="${RID:-linux-x64}"
IMAGE="softpipe-gl-build:debian12"

echo "==> Building image $IMAGE"
docker build -t "$IMAGE" -f "$REPO/build/docker/Dockerfile.linux" "$REPO/build/docker"

echo "==> Running containerised build (RID=$RID)"
docker run --rm \
    -v "$REPO:/work" \
    -e RID="$RID" \
    "$IMAGE" \
    bash /work/build/build-in-container.sh

echo "==> Artifact:"
ls -la "$REPO/artifacts/$RID/"
