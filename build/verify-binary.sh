#!/usr/bin/env bash
# Enforce the two hard requirements on the produced shared object:
#   1. It exports EXACTLY: eglGetProcAddress, vkGetInstanceProcAddr
#   2. It links only "standard" runtime deps -- crucially NO system LLVM.
#
# Usage: verify-binary.sh <path-to-libsoftpipe_gl.so>
set -euo pipefail

SO="${1:?path to .so required}"

echo "=================================================================="
echo " Verifying $SO"
echo "=================================================================="

fail=0

# --- 1. Exported symbols -----------------------------------------------------
# Global, defined dynamic symbols only (version script makes everything else
# local). nm marks global text symbols with an uppercase 'T'.
mapfile -t exported < <(nm -D --defined-only "$SO" \
    | awk '$2 ~ /^[TWD]$/ { print $3 }' | sort -u)

echo
echo "Exported (global, defined) dynamic symbols:"
printf '  %s\n' "${exported[@]:-<none>}"

expected=$'eglGetProcAddress\nvkGetInstanceProcAddr'
got=$(printf '%s\n' "${exported[@]:-}" | sort -u)
if [[ "$got" == "$expected" ]]; then
    echo "  -> OK: exactly the two required entry points are exported."
else
    echo "  -> FAIL: exported symbol set does not match the two required entry points."
    fail=1
fi

# --- 2. Runtime dependencies -------------------------------------------------
echo
echo "DT_NEEDED entries:"
mapfile -t needed < <(objdump -p "$SO" | awk '/NEEDED/ {print $2}' | sort -u)
printf '  %s\n' "${needed[@]}"

# Anything from the LLVM/Clang family is an automatic failure.
for n in "${needed[@]}"; do
    case "$n" in
        libLLVM*|libclang*|libLTO*|libPolly*|libMLIR*)
            echo "  -> FAIL: links system LLVM/Clang ($n)"
            fail=1
            ;;
    esac
done

# Allow-list of acceptable runtime deps. glibc + C++ runtime are the baseline;
# the rest are small, ubiquitous system libraries pulled by llvmpipe/lavapipe.
allow='^(libc|libm|libdl|libpthread|librt|libstdc\+\+|libgcc_s|ld-linux.*|libz|libzstd|libexpat|libdrm|libffi)\.so'
for n in "${needed[@]}"; do
    if ! [[ "$n" =~ $allow ]]; then
        echo "  -> WARNING: non-allowlisted dependency: $n"
    fi
done
if [[ $fail -eq 0 ]]; then
    echo "  -> OK: no system LLVM/Clang linkage."
fi

echo
if [[ $fail -ne 0 ]]; then
    echo "VERIFICATION FAILED"
    exit 1
fi
echo "VERIFICATION PASSED"
