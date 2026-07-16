/*
 * softpipe_gl: single self-contained software-rendering shared object.
 *
 * Bundles Mesa's llvmpipe (GL via EGL) and lavapipe (Vulkan) into one
 * shared library that exports exactly two entry points:
 *
 *   - eglGetProcAddress    (provided directly by the linked-in libEGL objects)
 *   - vkGetInstanceProcAddr (thin shim below, forwarding to lavapipe's ICD entry)
 *
 * Every other symbol is hidden by the accompanying version script
 * (softpipe_gl.sym) plus -Wl,--exclude-libs,ALL.
 */

/*
 * Provide the software pipe-screen constructor (sw_screen_create_vk) and the
 * (stub) DRM driver descriptors that Mesa's static pipe-loader references.
 * These are exactly the includes each stock Gallium target C file uses
 * (see dri_target.c / lavapipe_target.c). Because we do NOT link those target
 * wrappers, this is the single definition site -- no duplicate symbols.
 * -DGALLIUM_LLVMPIPE is supplied by the driver_swrast dependency.
 */
#include "util/detect_os.h"
#include "target-helpers/drm_helper.h"
#include "target-helpers/sw_helper.h"

#include <vulkan/vulkan_core.h>

/*
 * Provided by liblavapipe_st (linked with --whole-archive). This is the
 * standard Vulkan ICD entry point. With a NULL instance it resolves the
 * global commands (vkCreateInstance, vkEnumerateInstance*), which is exactly
 * the contract of vkGetInstanceProcAddr, so a straight forward works.
 */
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName);

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return vk_icdGetInstanceProcAddr(instance, pName);
}
