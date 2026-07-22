#!/usr/bin/env bash
# Build and run the GL <- Vulkan external-memory interop probe against the
# produced single .so, in a clean Debian 12 container (no Mesa installed).
#
# Usage: tests/interop/run.sh [RID]
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

docker build -q -t "$IMAGE" -f "$REPO/build/docker/Dockerfile.test" "$REPO/build/docker" >/dev/null
docker run --rm -v "$REPO:/work" "$IMAGE" bash -c '
    set -euo pipefail
    gcc -O2 -Wall -I /work/external/mesa/include \
        /work/tests/interop/interop.c -o /tmp/interop -ldl
    /tmp/interop '"/work/artifacts/$RID/libsoftmesa.so"'
'
