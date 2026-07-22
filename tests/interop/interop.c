/*
 * GL <- Vulkan external-memory interop probe for libsoftmesa.
 *
 * Because llvmpipe (GL) and lavapipe (Vulkan) are the same Gallium/LLVM
 * software stack in one library, a Vulkan memory allocation exported as an
 * opaque FD should be importable into OpenGL via GL_EXT_memory_object[_fd].
 *
 * This program:
 *   1. Reports the GL interop extensions (memory_object / semaphore, _fd).
 *   2. Reports the Vulkan external-memory extensions.
 *   3. Compares the GL and Vulkan device/driver UUIDs (must match to interop).
 *   4. Functional test: Vulkan allocates EXPORTABLE host-visible memory bound
 *      to a buffer, writes a known pattern, exports an opaque FD; GL imports
 *      that FD as a memory object, backs a GL buffer with it, reads it back,
 *      and byte-compares.
 *
 * Everything is loaded through the two exported entry points only.
 * Exit 0 = the functional import worked and the bytes matched.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

#define EGL_EGL_PROTOTYPES 0
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLES_PROTOTYPES 0
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

/* GL enums not always in gl2.h */
#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif
#ifndef GL_NUM_DEVICE_UUIDS_EXT
#define GL_NUM_DEVICE_UUIDS_EXT 0x9596
#endif

#define BUF_SIZE 4096

typedef void (*GLFuncPtr)(void);
typedef GLFuncPtr (*PFN_eglGetProcAddress)(const char *);

static PFN_eglGetProcAddress p_egl;
static PFN_vkGetInstanceProcAddr p_vk;

static void *egl(const char *n) { return (void *)p_egl(n); }

/* ------------------------------------------------------------------ GL side */
typedef EGLDisplay (*PFN_eglGetPlatformDisplay)(EGLenum, void *, const EGLAttrib *);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

typedef const GLubyte *(*PFN_glGetString)(GLenum);
typedef const GLubyte *(*PFN_glGetStringi)(GLenum, GLuint);
typedef void (*PFN_glGetIntegerv)(GLenum, GLint *);
typedef GLenum (*PFN_glGetError)(void);
typedef void (*PFN_glGetUnsignedBytei_vEXT)(GLenum, GLuint, GLubyte *);
typedef void (*PFN_glCreateMemoryObjectsEXT)(GLsizei, GLuint *);
typedef void (*PFN_glImportMemoryFdEXT)(GLuint, GLuint64, GLenum, GLint);
typedef void (*PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void (*PFN_glBindBuffer)(GLenum, GLuint);
typedef void (*PFN_glBufferStorageMemEXT)(GLenum, GLsizeiptr, GLuint, GLuint64);
typedef void (*PFN_glGetBufferSubData)(GLenum, GLintptr, GLsizeiptr, void *);

static int gl_has_ext(PFN_glGetStringi gsi, PFN_glGetIntegerv giv, const char *want)
{
    GLint n = 0; giv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const GLubyte *e = gsi(GL_EXTENSIONS, i);
        if (e && strcmp((const char *)e, want) == 0) return 1;
    }
    return 0;
}

/* -------------------------------------------------------------- Vulkan side */
#define GIPA(inst, name) ((PFN_##name)p_vk((inst), #name))
#define GDPA(dev, name) ((PFN_##name)vkGetDeviceProcAddr((dev), #name))

static uint32_t find_mem(VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static void hexuuid(const uint8_t *u, char *out)
{
    for (int i = 0; i < VK_UUID_SIZE; i++) sprintf(out + i * 2, "%02x", u[i]);
    out[VK_UUID_SIZE * 2] = 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *libpath = argc > 1 ? argv[1] : "./libsoftmesa.so";
    void *h = dlopen(libpath, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 2; }
    p_egl = (PFN_eglGetProcAddress)dlsym(h, "eglGetProcAddress");
    p_vk = (PFN_vkGetInstanceProcAddr)dlsym(h, "vkGetInstanceProcAddr");
    if (!p_egl || !p_vk) { fprintf(stderr, "missing exports\n"); return 2; }

    printf("== GL <- Vulkan external memory interop probe ==\n\n");

    /* ---- GL context (desktop GL for glGetBufferSubData) ---- */
    PFN_eglGetPlatformDisplay eglGetPlatformDisplay = egl("eglGetPlatformDisplay");
    PFN_eglInitialize eglInitialize = egl("eglInitialize");
    PFN_eglChooseConfig eglChooseConfig = egl("eglChooseConfig");
    PFN_eglBindAPI eglBindAPI = egl("eglBindAPI");
    PFN_eglCreateContext eglCreateContext = egl("eglCreateContext");
    PFN_eglMakeCurrent eglMakeCurrent = egl("eglMakeCurrent");

    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint mj, mn;
    if (!eglInitialize(dpy, &mj, &mn)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_API);
    const EGLint cfg_attribs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig cfg; EGLint num = 0;
    eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &num);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "GL context/makeCurrent failed\n"); return 1;
    }

    PFN_glGetString glGetString = egl("glGetString");
    PFN_glGetStringi glGetStringi = egl("glGetStringi");
    PFN_glGetIntegerv glGetIntegerv = egl("glGetIntegerv");
    PFN_glGetError glGetError = egl("glGetError");
    printf("[GL] renderer: %s\n", glGetString(GL_RENDERER));

    int gl_memobj    = gl_has_ext(glGetStringi, glGetIntegerv, "GL_EXT_memory_object");
    int gl_memobj_fd = gl_has_ext(glGetStringi, glGetIntegerv, "GL_EXT_memory_object_fd");
    int gl_sem       = gl_has_ext(glGetStringi, glGetIntegerv, "GL_EXT_semaphore");
    int gl_sem_fd    = gl_has_ext(glGetStringi, glGetIntegerv, "GL_EXT_semaphore_fd");
    printf("[GL] GL_EXT_memory_object    : %s\n", gl_memobj    ? "yes" : "NO");
    printf("[GL] GL_EXT_memory_object_fd : %s\n", gl_memobj_fd ? "yes" : "NO");
    printf("[GL] GL_EXT_semaphore        : %s\n", gl_sem       ? "yes" : "NO");
    printf("[GL] GL_EXT_semaphore_fd     : %s\n", gl_sem_fd    ? "yes" : "NO");

    char gl_dev_uuid[64] = "?", gl_drv_uuid[64] = "?";
    if (gl_memobj) {
        PFN_glGetUnsignedBytei_vEXT glGetUB = egl("glGetUnsignedBytei_vEXT");
        uint8_t u[16];
        glGetUB(GL_DEVICE_UUID_EXT, 0, u); hexuuid(u, gl_dev_uuid);
        glGetUB(GL_DRIVER_UUID_EXT, 0, u); hexuuid(u, gl_drv_uuid);
        printf("[GL] device UUID: %s\n[GL] driver UUID: %s\n", gl_dev_uuid, gl_drv_uuid);
    }

    /* ---- Vulkan device with external memory fd ---- */
    printf("\n");
    PFN_vkCreateInstance vkCreateInstance = GIPA(NULL, vkCreateInstance);
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance failed\n"); return 1; }

    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = GIPA(inst, vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = GIPA(inst, vkGetPhysicalDeviceProperties2);
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = GIPA(inst, vkGetPhysicalDeviceMemoryProperties);
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = GIPA(inst, vkEnumerateDeviceExtensionProperties);
    PFN_vkCreateDevice vkCreateDevice = GIPA(inst, vkCreateDevice);
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = GIPA(inst, vkGetDeviceProcAddr);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = GIPA(inst, vkGetPhysicalDeviceQueueFamilyProperties);

    uint32_t nd = 0; vkEnumeratePhysicalDevices(inst, &nd, NULL);
    if (!nd) { fprintf(stderr, "no vulkan device\n"); return 1; }
    VkPhysicalDevice pds[4]; if (nd > 4) nd = 4;
    vkEnumeratePhysicalDevices(inst, &nd, pds);
    VkPhysicalDevice pd = pds[0];

    VkPhysicalDeviceIDProperties idp = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 p2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &idp };
    vkGetPhysicalDeviceProperties2(pd, &p2);
    char vk_dev_uuid[64], vk_drv_uuid[64];
    hexuuid(idp.deviceUUID, vk_dev_uuid);
    hexuuid(idp.driverUUID, vk_drv_uuid);
    printf("[VK] device: %s\n", p2.properties.deviceName);
    printf("[VK] device UUID: %s\n[VK] driver UUID: %s\n", vk_dev_uuid, vk_drv_uuid);

    uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL);
    VkExtensionProperties *exts = calloc(ne, sizeof(*exts));
    vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, exts);
    int vk_extmem_fd = 0, vk_extsem_fd = 0, vk_extmem_host = 0, vk_extmem_dmabuf = 0;
    for (uint32_t i = 0; i < ne; i++) {
        if (!strcmp(exts[i].extensionName, "VK_KHR_external_memory_fd")) vk_extmem_fd = 1;
        if (!strcmp(exts[i].extensionName, "VK_KHR_external_semaphore_fd")) vk_extsem_fd = 1;
        if (!strcmp(exts[i].extensionName, "VK_EXT_external_memory_host")) vk_extmem_host = 1;
        if (!strcmp(exts[i].extensionName, "VK_EXT_external_memory_dma_buf")) vk_extmem_dmabuf = 1;
    }
    free(exts);
    printf("[VK] VK_KHR_external_memory_fd    : %s\n", vk_extmem_fd ? "yes" : "NO");
    printf("[VK] VK_KHR_external_semaphore_fd : %s\n", vk_extsem_fd ? "yes" : "NO");
    printf("[VK] VK_EXT_external_memory_host  : %s\n", vk_extmem_host ? "yes" : "NO");
    printf("[VK] VK_EXT_external_memory_dma_buf: %s\n", vk_extmem_dmabuf ? "yes" : "NO");

    printf("\n[UUID] GL driver == VK driver : %s\n",
           strcmp(gl_drv_uuid, vk_drv_uuid) == 0 ? "MATCH" : "differ");

    if (!(gl_memobj && gl_memobj_fd && vk_extmem_fd)) {
        printf("\nRESULT: capability probe only -- required extensions not all present.\n");
        return 1;
    }

    /* ---- Functional import: VK exportable memory -> GL memory object ---- */
    printf("\n[interop] Vulkan allocates exportable memory, writes a pattern, exports an opaque FD...\n");

    uint32_t qf = 0; uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio };
    const char *dev_exts[] = { "VK_KHR_external_memory", "VK_KHR_external_memory_fd" };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 2, .ppEnabledExtensionNames = dev_exts };
    VkDevice dev;
    if (vkCreateDevice(pd, &dci, NULL, &dev) != VK_SUCCESS) { fprintf(stderr, "vkCreateDevice failed\n"); return 1; }

    PFN_vkCreateBuffer vkCreateBuffer = GDPA(dev, vkCreateBuffer);
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = GDPA(dev, vkGetBufferMemoryRequirements);
    PFN_vkAllocateMemory vkAllocateMemory = GDPA(dev, vkAllocateMemory);
    PFN_vkBindBufferMemory vkBindBufferMemory = GDPA(dev, vkBindBufferMemory);
    PFN_vkMapMemory vkMapMemory = GDPA(dev, vkMapMemory);
    PFN_vkUnmapMemory vkUnmapMemory = GDPA(dev, vkUnmapMemory);
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR = GDPA(dev, vkGetMemoryFdKHR);
    if (!vkGetMemoryFdKHR) { fprintf(stderr, "vkGetMemoryFdKHR not found\n"); return 1; }

    VkPhysicalDeviceMemoryProperties memprops;
    vkGetPhysicalDeviceMemoryProperties(pd, &memprops);

    VkExternalMemoryBufferCreateInfo ext_bci = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &ext_bci,
        .size = BUF_SIZE, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer vbuf;
    if (vkCreateBuffer(dev, &bci, NULL, &vbuf) != VK_SUCCESS) { fprintf(stderr, "vkCreateBuffer failed\n"); return 1; }
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, vbuf, &mr);

    uint32_t mt = find_mem(&memprops, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) { fprintf(stderr, "no host-visible mem type\n"); return 1; }

    /* Give the allocation headroom: llvmpipe over-allocates memobj-backed
     * buffers by a rasterizer block, so the imported memory must be a little
     * larger than the GL buffer size. */
    VkDeviceSize alloc_size = mr.size + 65536;
    VkExportMemoryAllocateInfo exp = { .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &exp,
        .allocationSize = alloc_size, .memoryTypeIndex = mt };
    VkDeviceMemory vmem;
    if (vkAllocateMemory(dev, &mai, NULL, &vmem) != VK_SUCCESS) { fprintf(stderr, "vkAllocateMemory(exportable) failed\n"); return 1; }
    vkBindBufferMemory(dev, vbuf, vmem, 0);

    /* Write a recognizable pattern through the Vulkan mapping. */
    uint8_t pattern[BUF_SIZE];
    for (int i = 0; i < BUF_SIZE; i++) pattern[i] = (uint8_t)(i * 7 + 13);
    void *map = NULL;
    vkMapMemory(dev, vmem, 0, BUF_SIZE, 0, &map);
    memcpy(map, pattern, BUF_SIZE);
    vkUnmapMemory(dev, vmem);

    VkMemoryGetFdInfoKHR gfi = { .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = vmem, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT };
    int fd = -1;
    if (vkGetMemoryFdKHR(dev, &gfi, &fd) != VK_SUCCESS || fd < 0) { fprintf(stderr, "vkGetMemoryFdKHR failed\n"); return 1; }
    printf("[interop] exported opaque FD = %d (size %llu)\n", fd, (unsigned long long)mr.size);

    /* GL imports the FD and backs a buffer with it. */
    printf("[interop] OpenGL imports the FD as a memory object and backs a GL buffer...\n");
    PFN_glCreateMemoryObjectsEXT glCreateMemoryObjectsEXT = egl("glCreateMemoryObjectsEXT");
    PFN_glImportMemoryFdEXT glImportMemoryFdEXT = egl("glImportMemoryFdEXT");
    PFN_glGenBuffers glGenBuffers = egl("glGenBuffers");
    PFN_glBindBuffer glBindBuffer = egl("glBindBuffer");
    PFN_glBufferStorageMemEXT glBufferStorageMemEXT = egl("glBufferStorageMemEXT");
    PFN_glGetBufferSubData glGetBufferSubData = egl("glGetBufferSubData");
    if (!glCreateMemoryObjectsEXT || !glImportMemoryFdEXT || !glBufferStorageMemEXT || !glGetBufferSubData) {
        fprintf(stderr, "GL interop entry points missing\n"); return 1;
    }

    while (glGetError() != GL_NO_ERROR) {}
    GLuint memobj = 0; glCreateMemoryObjectsEXT(1, &memobj);
    glImportMemoryFdEXT(memobj, alloc_size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
    GLenum e1 = glGetError();
    if (e1 != GL_NO_ERROR) { fprintf(stderr, "glImportMemoryFdEXT error 0x%x\n", e1); return 1; }

    GLuint gbuf = 0; glGenBuffers(1, &gbuf);
    glBindBuffer(GL_ARRAY_BUFFER, gbuf);
    glBufferStorageMemEXT(GL_ARRAY_BUFFER, BUF_SIZE, memobj, 0);
    GLenum e2 = glGetError();
    if (e2 != GL_NO_ERROR) { fprintf(stderr, "glBufferStorageMemEXT error 0x%x\n", e2); return 1; }

    uint8_t readback[BUF_SIZE];
    memset(readback, 0, sizeof(readback));
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, BUF_SIZE, readback);
    GLenum e3 = glGetError();
    if (e3 != GL_NO_ERROR) { fprintf(stderr, "glGetBufferSubData error 0x%x\n", e3); return 1; }

    int mismatch = memcmp(readback, pattern, BUF_SIZE);
    printf("[interop] GL read back %d bytes; first bytes: %02x %02x %02x %02x (expected %02x %02x %02x %02x)\n",
           BUF_SIZE, readback[0], readback[1], readback[2], readback[3],
           pattern[0], pattern[1], pattern[2], pattern[3]);

    if (mismatch == 0) {
        printf("\nRESULT: PASS -- Vulkan-exported memory was imported into OpenGL and the bytes matched.\n");
        printf("        GL<-VK external memory interop WORKS in this build.\n");
        return 0;
    }
    printf("\nRESULT: import succeeded but data did NOT match (mismatch at byte %d).\n",
           (int)(memcmp(readback, pattern, 1) ? 0 : 1));
    return 1;
}
