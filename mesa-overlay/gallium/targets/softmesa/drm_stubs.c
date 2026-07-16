/*
 * libdrm stubs.
 *
 * Mesa's DRI loader / pipe-loader / kms-swrast winsys reference a handful of
 * libdrm entry points (device enumeration, KMS mode-setting, PRIME buffer
 * sharing, syncobjs). A surfaceless *offscreen* software context never calls
 * any of them -- they are only link-time references from code paths that are
 * unreachable without a real DRM device.
 *
 * By defining these symbols here (they appear on the link line before -ldrm),
 * the linker resolves them from this object and, thanks to -Wl,--as-needed,
 * drops the libdrm.so.2 DT_NEEDED entry altogether. The result is a cleaner
 * dependency list for a library whose entire purpose is offscreen rendering.
 *
 * Every stub returns 0 / NULL, i.e. "no DRM device / operation failed", which
 * is precisely the behaviour we want should any of them ever be reached: Mesa
 * then falls back to the software (swrast) path.
 *
 * Regenerate the list after a Mesa bump by comparing the .so's undefined
 * dynamic symbols (nm -D --undefined-only) against libdrm's exported symbols
 * (nm -D --defined-only on libdrm.so.2) and keeping the intersection.
 * Any newly-referenced symbol that is missing here simply re-introduces the
 * libdrm dependency (harmless -- it stays on the allow-list), it does not break
 * the build.
 */

/* These stubs intentionally have no header-provided prototypes. */
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

/* Returning long covers both int-returning and pointer-returning callers on
 * LP64 (0 == NULL). The definitions deliberately take no parameters; C symbol
 * resolution is by name only, and the real call sites use libdrm's prototypes. */
#define DRM_STUB(name) long name(void) { return 0; }

DRM_STUB(drmAuthMagic)
DRM_STUB(drmCommandWriteRead)
DRM_STUB(drmCrtcGetSequence)
DRM_STUB(drmCrtcQueueSequence)
DRM_STUB(drmDevicesEqual)
DRM_STUB(drmFreeDevice)
DRM_STUB(drmFreeDevices)
DRM_STUB(drmFreeVersion)
DRM_STUB(drmGetCap)
DRM_STUB(drmGetDevice2)
DRM_STUB(drmGetDevices2)
DRM_STUB(drmGetPrimaryDeviceNameFromFd)
DRM_STUB(drmGetRenderDeviceNameFromFd)
DRM_STUB(drmGetVersion)
DRM_STUB(drmHandleEvent)
DRM_STUB(drmIoctl)
DRM_STUB(drmModeAtomicAddProperty)
DRM_STUB(drmModeAtomicAlloc)
DRM_STUB(drmModeAtomicCommit)
DRM_STUB(drmModeAtomicFree)
DRM_STUB(drmModeConnectorSetProperty)
DRM_STUB(drmModeCreatePropertyBlob)
DRM_STUB(drmModeFreeConnector)
DRM_STUB(drmModeFreeCrtc)
DRM_STUB(drmModeFreeEncoder)
DRM_STUB(drmModeFreeObjectProperties)
DRM_STUB(drmModeFreePlane)
DRM_STUB(drmModeFreePlaneResources)
DRM_STUB(drmModeFreeProperty)
DRM_STUB(drmModeFreePropertyBlob)
DRM_STUB(drmModeFreeResources)
DRM_STUB(drmModeGetConnector)
DRM_STUB(drmModeGetConnectorCurrent)
DRM_STUB(drmModeGetCrtc)
DRM_STUB(drmModeGetEncoder)
DRM_STUB(drmModeGetPlane)
DRM_STUB(drmModeGetPlaneResources)
DRM_STUB(drmModeGetProperty)
DRM_STUB(drmModeGetPropertyBlob)
DRM_STUB(drmModeGetResources)
DRM_STUB(drmModeObjectGetProperties)
DRM_STUB(drmPrimeFDToHandle)
DRM_STUB(drmPrimeHandleToFD)
DRM_STUB(drmSetClientCap)
DRM_STUB(drmSyncobjDestroy)
DRM_STUB(drmSyncobjFDToHandle)
DRM_STUB(drmSyncobjSignal)
