#!/usr/bin/env bash
# Build and run the smoke test against the produced single .so in a clean
# Debian 12 container that has no Mesa installed.
#
# Usage: tests/smoke/run.sh [RID]
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd "$SCRIPT_DIR/../.." && pwd)
RID="${1:-linux-x64}"
SO="$REPO/artifacts/$RID/libsoftmesa.so"
IMAGE="softpipe-gl-test:debian12"

if [[ ! -f "$SO" ]]; then
    echo "Artifact not found: $SO (run build/build-linux.sh first)" >&2
    exit 1
fi

echo "==> Building test image $IMAGE"
docker build -t "$IMAGE" -f "$REPO/build/docker/Dockerfile.test" "$REPO/build/docker"

echo "==> Compiling and running smoke test in clean container"
docker run --rm -v "$REPO:/work" "$IMAGE" bash -c '
    set -euo pipefail
    gcc -O2 -Wall -I /work/external/mesa/include \
        /work/tests/smoke/smoke.c -o /tmp/smoke -ldl
    echo "--- ldd of the smoke binary (host toolchain only) ---"
    ldd /tmp/smoke || true
    echo "--- running ---"
    /tmp/smoke '"/work/artifacts/$RID/libsoftmesa.so"'
'
