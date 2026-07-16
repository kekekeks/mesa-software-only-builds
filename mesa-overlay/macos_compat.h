/*
 * macOS compatibility shim, force-included into every Mesa TU on the macOS
 * build (via CFLAGS -include). Forcing HAVE_LIBDRM to unlock the surfaceless
 * dri/EGL stack enables a few Linux-only code paths that a headless software
 * context never actually exercises; provide portable stand-ins so they compile.
 */
#pragma once

#if defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <time.h>

/* macOS has poll() but not ppoll(). This path is only reached for real DRM
 * dma-buf fence waits, which never happen in an offscreen software context. */
static inline int
ppoll(struct pollfd *fds, nfds_t nfds,
      const struct timespec *timeout_ts, const sigset_t *sigmask)
{
   (void)sigmask;
   int timeout_ms = -1;
   if (timeout_ts)
      timeout_ms = (int)(timeout_ts->tv_sec * 1000 +
                         timeout_ts->tv_nsec / 1000000);
   return poll(fds, nfds, timeout_ms);
}
#endif
