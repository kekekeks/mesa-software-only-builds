# softpipe-gl

A single, self-contained software-rendering shared library built from
[Mesa](https://gitlab.freedesktop.org/mesa/mesa). It bundles **llvmpipe**
(OpenGL, via EGL) and **lavapipe** (Vulkan) into one `.so`/`.dylib`/`.dll`
that exports exactly two entry points:

| Symbol | Backend |
| --- | --- |
| `eglGetProcAddress` | llvmpipe (OpenGL / OpenGL ES via EGL) |
| `vkGetInstanceProcAddr` | lavapipe (Vulkan) |

Everything else — including the whole of LLVM — is linked **statically** and
hidden. A consumer `dlopen`s/`LoadLibrary`s the library, resolves those two
symbols, and loads the rest of the GL/Vulkan API through them.

Supported runtimes: **linux-x64** (Debian 12 container, glibc), **win-x64**
(MSYS2/MinGW), **osx-arm64** and **osx-x64** (Homebrew, on `macos-14` /
`macos-13` runners). All build the identical two entry points and render a quad
offscreen through both the GL and Vulkan paths.

## Guarantees (enforced by `build/verify-binary.sh`)

- `objdump -T` shows **only** `eglGetProcAddress` and `vkGetInstanceProcAddr`.
- No dependency on a system LLVM (`libLLVM`). The only runtime deps are the
  standard C/C++ runtime plus a handful of ubiquitous base libraries
  (`libc`, `libstdc++`, `libgcc_s`, `libm`, `libz`, `libzstd`, `libexpat`,
  `libdrm`).

The Linux binary is built inside a **Debian 12** container so it stays
loadable on any modern (Debian-12-era glibc) Linux.

## Layout

```
external/mesa/                     # Mesa submodule, pinned to a release tag
mesa-overlay/
  gallium/targets/softmesa/     # the combined single-.so Meson target
    meson.build                    #   links llvmpipe + lavapipe static libs
    softmesa.c                  #   target-helpers + vkGetInstanceProcAddr shim
    softmesa.sym                #   version script: exports only the 2 symbols
build/
  docker/Dockerfile.linux          # Debian 12 build image (LLVM 19 static, meson)
  docker/Dockerfile.test           # clean Debian 12 image (no mesa) for smoke test
  apply-overlay.sh                 # injects the overlay into a Mesa source tree
  build-in-container.sh            # meson setup + ninja + stage + verify
  build-linux.sh                   # host entry point (builds image, runs build)
  verify-binary.sh                 # enforces the export/dependency guarantees
tests/smoke/                       # dlopen + render-a-quad-offscreen test (GL + VK)
artifacts/<rid>/libsoftmesa.so  # build output (gitignored)
```

## Build (Linux)

```sh
git clone --recurse-submodules <repo> softpipe-gl
cd softpipe-gl
./build/build-linux.sh
```

Output: `artifacts/linux-x64/libsoftmesa.so`.

## Smoke test

```sh
./tests/smoke/run.sh linux-x64
```

Compiles `tests/smoke/smoke.c` in a clean Debian 12 container (no Mesa
installed), `dlopen`s the produced `.so`, and renders a quad offscreen through
both the EGL/GLES2 (llvmpipe) and Vulkan (lavapipe) paths, reading the pixels
back to verify.

## Versioning

Package versions track the **pinned Mesa release** (`external/mesa/VERSION`)
with the CI run number as a 4th component, so a version says exactly which Mesa
a binary came from:

| Build | Version | Example |
| --- | --- | --- |
| CI (`unofficial.mesa.softwarerenderer` packages) | `<mesa>.<gh-run-number>` | `25.3.6.42` |
| Local `pack-nuget.sh` | `<mesa>-local` (prerelease) | `25.3.6-local` |

Ordering follows NuGet semantics: bumping the Mesa submodule (`25.3.7.x`)
always sorts above any `25.3.6.x`, and within one Mesa version a higher run
number sorts higher. Override with `VERSION=…` (full) or `BUILD_NUMBER=…` /
`MESA_VERSION=…` for the pieces. Bumping Mesa is therefore just: move the
submodule to the new release tag and the package version follows automatically.

## How the single `.so` is produced

Modern Mesa links the Gallium DRI driver directly into `libEGL`, and lavapipe
is a self-contained Vulkan ICD. The overlay adds one extra Meson target that
links the *static* internal libraries of both stacks together:

- libEGL's own objects (they export `eglGetProcAddress`),
- the DRI frontend + `libgallium` + `libllvmpipe` (llvmpipe),
- `liblavapipe_st` (lavapipe),

with `-Dshared-llvm=disabled` (static LLVM), a linker version script that keeps
only the two symbols, and `-Wl,--exclude-libs,ALL`. A tiny shim exposes
`vkGetInstanceProcAddr` by forwarding to lavapipe's `vk_icdGetInstanceProcAddr`.

Mesa version and toolchain versions are pinned in the submodule and the build
Dockerfile respectively.
```
