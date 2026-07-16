#!/usr/bin/env python3
"""Inject small macOS compat shims into Mesa sources that assume Linux.

Forcing HAVE_LIBDRM (to unlock the surfaceless EGL/dri stack on macOS) compiles
a few Linux-only code paths that a headless software context never runs. Rather
than force-include a header globally (which corrupts Meson's feature probes), we
patch the specific files. Each patch is idempotent.
"""
import sys
import pathlib

mesa = pathlib.Path(sys.argv[1])

PPOLL_SHIM = """
#if defined(__APPLE__)
#include <signal.h>
/* macOS has poll() but not ppoll(); only reached for real DRM fence waits. */
static inline int
ppoll(struct pollfd *fds, nfds_t nfds,
      const struct timespec *to, const sigset_t *sigmask)
{
   (void)sigmask;
   int ms = to ? (int)(to->tv_sec * 1000 + to->tv_nsec / 1000000) : -1;
   return poll(fds, nfds, ms);
}
#endif
"""


def inject_after(path, anchor, block, marker):
    p = mesa / path
    text = p.read_text()
    if marker in text:
        print(f"  {path}: already patched")
        return
    assert anchor in text, f"anchor not found in {path}: {anchor!r}"
    text = text.replace(anchor, anchor + block, 1)
    p.write_text(text)
    print(f"  {path}: patched")


# llvmpipe fence waiting uses ppoll under HAVE_LIBDRM.
inject_after(
    "src/gallium/drivers/llvmpipe/lp_fence.c",
    "#include <poll.h>",
    PPOLL_SHIM,
    "static inline int\nppoll",
)

print("macOS source patches applied.")
