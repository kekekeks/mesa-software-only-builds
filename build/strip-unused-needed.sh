#!/usr/bin/env bash
# Remove DT_NEEDED entries from a shared object that contribute ZERO symbols
# to it (i.e. the .so imports nothing from them). This is safe by construction:
# a library from which no symbol is imported cannot affect runtime behaviour.
#
# In our case the libdrm stubs (drm_stubs.c) resolve every drm* reference
# internally, so libdrm ends up contributing nothing -- but the linker still
# records it as NEEDED because it is passed by full path. This drops it.
#
# Usage: strip-unused-needed.sh <path-to.so>
set -euo pipefail
SO="${1:?path to .so required}"

# Undefined (imported) dynamic symbols of the .so.
und=$(nm -D --undefined-only "$SO" | awk '{print $NF}' | sort -u)

mapfile -t needed < <(objdump -p "$SO" | awk '/NEEDED/{print $2}')
for lib in "${needed[@]}"; do
    # Never touch the core C/C++ runtime or the dynamic loader.
    case "$lib" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|\
        libstdc++.so*|libgcc_s.so*|ld-linux*) continue ;;
    esac

    # Locate the library so we can read its exported symbols.
    path=$(ldconfig -p 2>/dev/null | awk -v l="$lib" '$1==l {print $NF; exit}')
    if [[ -z "$path" || ! -e "$path" ]]; then
        echo "  keep $lib (not found on system, cannot analyse)"
        continue
    fi

    defs=$(nm -D --defined-only "$path" 2>/dev/null | awk '{print $NF}' | sort -u)
    # Any symbol both imported by the .so and exported by this library?
    if comm -12 <(printf '%s\n' "$und") <(printf '%s\n' "$defs") | grep -q .; then
        echo "  keep $lib (provides used symbols)"
    else
        echo "  drop $lib (contributes no symbols)"
        patchelf --remove-needed "$lib" "$SO"
    fi
done
