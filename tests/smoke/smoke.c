/*
 * Smoke test for libsoftpipe_gl.so.
 *
 * Loads the single shared object via dlopen and resolves ONLY the two exported
 * entry points (eglGetProcAddress, vkGetInstanceProcAddr). Everything else is
 * fetched through those. It then renders a quad into an offscreen buffer twice:
 *
 *   1. OpenGL ES 2.0 via EGL (surfaceless) + an FBO  -> llvmpipe
 *   2. Vulkan into an offscreen VkImage              -> lavapipe
 *
 * In both cases the rendered pixels are read back and checked, proving the
 * self-contained software stack actually executes.
 *
 * Exit code 0 = both paths rendered and verified.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE lib_t;
static lib_t load_lib(const char *p) { return LoadLibraryA(p); }
static void  *lib_sym(lib_t h, const char *n) { return (void *)GetProcAddress(h, n); }
static const char *lib_err(void) { return "LoadLibrary/GetProcAddress failed"; }
#else
#include <dlfcn.h>
typedef void *lib_t;
static lib_t load_lib(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void  *lib_sym(lib_t h, const char *n) { return dlsym(h, n); }
static const char *lib_err(void) { return dlerror(); }
#endif
#include <stdint.h>

/* We only ever call through function pointers we load ourselves; disable the
 * header prototypes so nothing tries to bind against a system libEGL/libGL. */
#define EGL_EGL_PROTOTYPES 0
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLES_PROTOTYPES 0
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "shaders/quad.vert.spv.h"
#include "shaders/quad.frag.spv.h"

#define W 64
#define H 64

/* The constant color both shaders write: vec4(0.25, 0.50, 1.00, 1.0). */
#define EXPECT_R 64
#define EXPECT_G 128
#define EXPECT_B 255
#define EXPECT_A 255
/* llvmpipe/lavapipe rounding can differ by a bit; allow a small tolerance. */
#define TOL 4

static int near_expected(const uint8_t *p)
{
    return abs((int)p[0] - EXPECT_R) <= TOL &&
           abs((int)p[1] - EXPECT_G) <= TOL &&
           abs((int)p[2] - EXPECT_B) <= TOL &&
           abs((int)p[3] - EXPECT_A) <= TOL;
}

/* ========================================================================== */
/* EGL / OpenGL ES 2.0 path                                                    */
/* ========================================================================== */

typedef void (*GLproc)(void);
typedef GLproc (*PFN_eglGetProcAddress)(const char *);

/* EGL entry points */
typedef EGLDisplay (*PFN_eglGetPlatformDisplay)(EGLenum, void *, const EGLAttrib *);
typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay, EGLint *, EGLint *);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLBoolean (*PFN_eglBindAPI)(EGLenum);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLint     (*PFN_eglGetError)(void);
typedef const char*(*PFN_eglQueryString)(EGLDisplay, EGLint);

/* GLES2 entry points we use */
typedef void  (*PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void  (*PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void  (*PFN_glGenRenderbuffers)(GLsizei, GLuint *);
typedef void  (*PFN_glBindRenderbuffer)(GLenum, GLuint);
typedef void  (*PFN_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void  (*PFN_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum(*PFN_glCheckFramebufferStatus)(GLenum);
typedef void  (*PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void  (*PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void  (*PFN_glClear)(GLbitfield);
typedef void  (*PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef GLuint(*PFN_glCreateShader)(GLenum);
typedef void  (*PFN_glShaderSource)(GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void  (*PFN_glCompileShader)(GLuint);
typedef void  (*PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef void  (*PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint(*PFN_glCreateProgram)(void);
typedef void  (*PFN_glAttachShader)(GLuint, GLuint);
typedef void  (*PFN_glLinkProgram)(GLuint);
typedef void  (*PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void  (*PFN_glUseProgram)(GLuint);
typedef GLint (*PFN_glGetAttribLocation)(GLuint, const GLchar *);
typedef void  (*PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void  (*PFN_glBindBuffer)(GLenum, GLuint);
typedef void  (*PFN_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void  (*PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void  (*PFN_glEnableVertexAttribArray)(GLuint);
typedef void  (*PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef GLenum(*PFN_glGetError)(void);

static PFN_eglGetProcAddress p_eglGetProcAddress;

static void *egl(const char *n)
{
    void *f = (void *)p_eglGetProcAddress(n);
    if (!f) { fprintf(stderr, "  eglGetProcAddress(%s) -> NULL\n", n); }
    return f;
}

static const char *gl_vs =
    "attribute vec2 pos;\n"
    "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *gl_fs =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.25, 0.50, 1.00, 1.0); }\n";

static int compile_gl_shader(PFN_glCreateShader cs, PFN_glShaderSource ss,
                             PFN_glCompileShader comp, PFN_glGetShaderiv giv,
                             PFN_glGetShaderInfoLog gil, GLenum type, const char *src)
{
    GLuint sh = cs(type);
    ss(sh, 1, &src, NULL);
    comp(sh);
    GLint ok = 0;
    giv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        gil(sh, sizeof(log), NULL, log);
        fprintf(stderr, "  shader compile failed: %s\n", log);
        return 0;
    }
    return (int)sh;
}

static int run_gl_test(void)
{
    printf("[EGL/GLES2] loading entry points via eglGetProcAddress...\n");

    PFN_eglGetPlatformDisplay eglGetPlatformDisplay = egl("eglGetPlatformDisplay");
    PFN_eglGetDisplay   eglGetDisplay   = egl("eglGetDisplay");
    PFN_eglInitialize   eglInitialize   = egl("eglInitialize");
    PFN_eglChooseConfig eglChooseConfig = egl("eglChooseConfig");
    PFN_eglBindAPI      eglBindAPI      = egl("eglBindAPI");
    PFN_eglCreateContext eglCreateContext = egl("eglCreateContext");
    PFN_eglCreatePbufferSurface eglCreatePbufferSurface = egl("eglCreatePbufferSurface");
    PFN_eglMakeCurrent  eglMakeCurrent  = egl("eglMakeCurrent");
    PFN_eglQueryString  eglQueryString  = egl("eglQueryString");

    if (!eglInitialize || !eglChooseConfig ||
        !eglBindAPI || !eglCreateContext || !eglMakeCurrent) {
        fprintf(stderr, "[EGL/GLES2] FAILED to resolve core EGL entry points "
                        "(EGL_KHR_(client_)get_all_proc_addresses missing?)\n");
        return 1;
    }

    /* On Windows, EGL is WGL-backed and the surfaceless platform is not
     * available (eglGetPlatformDisplay would hand back a non-functional
     * display), so use the default display. Elsewhere, use the surfaceless
     * platform (llvmpipe). We render into an FBO either way, so the surface
     * only needs to make a context current. */
    EGLDisplay dpy = EGL_NO_DISPLAY;
#ifndef _WIN32
    if (eglGetPlatformDisplay)
        dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                    EGL_DEFAULT_DISPLAY, NULL);
#endif
    if (dpy == EGL_NO_DISPLAY && eglGetDisplay)
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "[EGL] no display\n"); return 1; }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) { fprintf(stderr, "[EGL] init failed\n"); return 1; }
    printf("[EGL] initialized EGL %d.%d\n", major, minor);
    if (eglQueryString)
        printf("[EGL] vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));

    if (!eglBindAPI(EGL_OPENGL_ES_API)) { fprintf(stderr, "[EGL] bindAPI failed\n"); return 1; }

    const EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig config;
    EGLint num = 0;
    if (!eglChooseConfig(dpy, cfg_attribs, &config, 1, &num) || num < 1) {
        fprintf(stderr, "[EGL] chooseConfig failed\n"); return 1;
    }

    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) { fprintf(stderr, "[EGL] createContext failed\n"); return 1; }

    /* Prefer a surfaceless context (EGL_KHR_surfaceless_context). If the
     * implementation needs a surface (e.g. WGL-backed EGL on Windows), fall
     * back to a small pbuffer. We render into an FBO regardless. */
    if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        printf("[GLES2] context current (surfaceless)\n");
    } else if (eglCreatePbufferSurface) {
        const EGLint pb_attribs[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
        EGLSurface pb = eglCreatePbufferSurface(dpy, config, pb_attribs);
        if (pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx)) {
            fprintf(stderr, "[EGL] makeCurrent(pbuffer) failed\n"); return 1;
        }
        printf("[GLES2] context current (pbuffer)\n");
    } else {
        fprintf(stderr, "[EGL] makeCurrent failed and no pbuffer fallback\n");
        return 1;
    }

    /* Resolve the GL functions (also via eglGetProcAddress). */
    PFN_glGenFramebuffers glGenFramebuffers = egl("glGenFramebuffers");
    PFN_glBindFramebuffer glBindFramebuffer = egl("glBindFramebuffer");
    PFN_glGenRenderbuffers glGenRenderbuffers = egl("glGenRenderbuffers");
    PFN_glBindRenderbuffer glBindRenderbuffer = egl("glBindRenderbuffer");
    PFN_glRenderbufferStorage glRenderbufferStorage = egl("glRenderbufferStorage");
    PFN_glFramebufferRenderbuffer glFramebufferRenderbuffer = egl("glFramebufferRenderbuffer");
    PFN_glCheckFramebufferStatus glCheckFramebufferStatus = egl("glCheckFramebufferStatus");
    PFN_glViewport glViewport = egl("glViewport");
    PFN_glClearColor glClearColor = egl("glClearColor");
    PFN_glClear glClear = egl("glClear");
    PFN_glReadPixels glReadPixels = egl("glReadPixels");
    PFN_glCreateShader glCreateShader = egl("glCreateShader");
    PFN_glShaderSource glShaderSource = egl("glShaderSource");
    PFN_glCompileShader glCompileShader = egl("glCompileShader");
    PFN_glGetShaderiv glGetShaderiv = egl("glGetShaderiv");
    PFN_glGetShaderInfoLog glGetShaderInfoLog = egl("glGetShaderInfoLog");
    PFN_glCreateProgram glCreateProgram = egl("glCreateProgram");
    PFN_glAttachShader glAttachShader = egl("glAttachShader");
    PFN_glLinkProgram glLinkProgram = egl("glLinkProgram");
    PFN_glGetProgramiv glGetProgramiv = egl("glGetProgramiv");
    PFN_glUseProgram glUseProgram = egl("glUseProgram");
    PFN_glGetAttribLocation glGetAttribLocation = egl("glGetAttribLocation");
    PFN_glGenBuffers glGenBuffers = egl("glGenBuffers");
    PFN_glBindBuffer glBindBuffer = egl("glBindBuffer");
    PFN_glBufferData glBufferData = egl("glBufferData");
    PFN_glVertexAttribPointer glVertexAttribPointer = egl("glVertexAttribPointer");
    PFN_glEnableVertexAttribArray glEnableVertexAttribArray = egl("glEnableVertexAttribArray");
    PFN_glDrawArrays glDrawArrays = egl("glDrawArrays");
    PFN_glGetError glGetError = egl("glGetError");

    if (!glGenFramebuffers || !glDrawArrays || !glReadPixels || !glCreateShader) {
        fprintf(stderr, "[GLES2] FAILED to resolve GL entry points\n"); return 1;
    }

    GLuint fbo = 0, rb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[GLES2] FBO incomplete\n"); return 1;
    }

    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int vs = compile_gl_shader(glCreateShader, glShaderSource, glCompileShader,
                               glGetShaderiv, glGetShaderInfoLog, GL_VERTEX_SHADER, gl_vs);
    int fs = compile_gl_shader(glCreateShader, glShaderSource, glCompileShader,
                               glGetShaderiv, glGetShaderInfoLog, GL_FRAGMENT_SHADER, gl_fs);
    if (!vs || !fs) return 1;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, (GLuint)vs);
    glAttachShader(prog, (GLuint)fs);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { fprintf(stderr, "[GLES2] link failed\n"); return 1; }
    glUseProgram(prog);

    const GLfloat verts[] = { -1,-1,  1,-1,  -1,1,  1,1 };
    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    GLint loc = glGetAttribLocation(prog, "pos");
    if (loc < 0) loc = 0;
    glEnableVertexAttribArray((GLuint)loc);
    glVertexAttribPointer((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { fprintf(stderr, "[GLES2] GL error 0x%x\n", err); return 1; }

    uint8_t *px = malloc(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    const uint8_t *c = &px[(H / 2 * W + W / 2) * 4];
    printf("[GLES2] center pixel RGBA = %d,%d,%d,%d\n", c[0], c[1], c[2], c[3]);
    int ok = near_expected(c);
    free(px);

    if (!ok) { fprintf(stderr, "[GLES2] FAILED: unexpected pixel\n"); return 1; }
    printf("[GLES2] PASS: quad rendered offscreen and verified\n");
    return 0;
}

/* ========================================================================== */
/* Vulkan path                                                                 */
/* ========================================================================== */

static PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;

#define VK_CHECK(expr) do { VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { fprintf(stderr, "[Vulkan] %s -> VkResult %d\n", #expr, _r); return 1; } } while (0)

static uint32_t find_mem_type(VkPhysicalDeviceMemoryProperties *mp,
                              uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static int run_vk_test(void)
{
    printf("[Vulkan] loading entry points via vkGetInstanceProcAddr...\n");

#define GIPA(inst, name) ((PFN_##name)p_vkGetInstanceProcAddr((inst), #name))
    PFN_vkCreateInstance vkCreateInstance = GIPA(NULL, vkCreateInstance);
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = GIPA(NULL, vkEnumerateInstanceVersion);
    if (!vkCreateInstance) { fprintf(stderr, "[Vulkan] no vkCreateInstance\n"); return 1; }

    uint32_t apiver = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&apiver);

    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "softpipe-gl-smoke", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app };
    VkInstance inst;
    VK_CHECK(vkCreateInstance(&ici, NULL, &inst));

    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = GIPA(inst, vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = GIPA(inst, vkGetPhysicalDeviceProperties);
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = GIPA(inst, vkGetPhysicalDeviceQueueFamilyProperties);
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = GIPA(inst, vkGetPhysicalDeviceMemoryProperties);
    PFN_vkCreateDevice vkCreateDevice = GIPA(inst, vkCreateDevice);
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = GIPA(inst, vkGetDeviceProcAddr);

    uint32_t ndev = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(inst, &ndev, NULL));
    if (ndev == 0) { fprintf(stderr, "[Vulkan] no physical devices\n"); return 1; }
    VkPhysicalDevice *pds = calloc(ndev, sizeof(*pds));
    VK_CHECK(vkEnumeratePhysicalDevices(inst, &ndev, pds));
    VkPhysicalDevice pd = pds[0];
    free(pds);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pd, &props);
    printf("[Vulkan] physical device: %s (api %u.%u.%u)\n", props.deviceName,
           VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
           VK_VERSION_PATCH(props.apiVersion));

    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nqf, NULL);
    VkQueueFamilyProperties *qf = calloc(nqf, sizeof(*qf));
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nqf, qf);
    uint32_t qfam = UINT32_MAX;
    for (uint32_t i = 0; i < nqf; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; }
    free(qf);
    if (qfam == UINT32_MAX) { fprintf(stderr, "[Vulkan] no graphics queue\n"); return 1; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qfam, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice dev;
    VK_CHECK(vkCreateDevice(pd, &dci, NULL, &dev));

#define GDPA(name) ((PFN_##name)vkGetDeviceProcAddr(dev, #name))
    PFN_vkGetDeviceQueue vkGetDeviceQueue = GDPA(vkGetDeviceQueue);
    PFN_vkCreateImage vkCreateImage = GDPA(vkCreateImage);
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements = GDPA(vkGetImageMemoryRequirements);
    PFN_vkAllocateMemory vkAllocateMemory = GDPA(vkAllocateMemory);
    PFN_vkBindImageMemory vkBindImageMemory = GDPA(vkBindImageMemory);
    PFN_vkCreateImageView vkCreateImageView = GDPA(vkCreateImageView);
    PFN_vkCreateRenderPass vkCreateRenderPass = GDPA(vkCreateRenderPass);
    PFN_vkCreateFramebuffer vkCreateFramebuffer = GDPA(vkCreateFramebuffer);
    PFN_vkCreateShaderModule vkCreateShaderModule = GDPA(vkCreateShaderModule);
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = GDPA(vkCreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = GDPA(vkCreateGraphicsPipelines);
    PFN_vkCreateBuffer vkCreateBuffer = GDPA(vkCreateBuffer);
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = GDPA(vkGetBufferMemoryRequirements);
    PFN_vkBindBufferMemory vkBindBufferMemory = GDPA(vkBindBufferMemory);
    PFN_vkMapMemory vkMapMemory = GDPA(vkMapMemory);
    PFN_vkCreateCommandPool vkCreateCommandPool = GDPA(vkCreateCommandPool);
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = GDPA(vkAllocateCommandBuffers);
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = GDPA(vkBeginCommandBuffer);
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = GDPA(vkCmdBeginRenderPass);
    PFN_vkCmdBindPipeline vkCmdBindPipeline = GDPA(vkCmdBindPipeline);
    PFN_vkCmdDraw vkCmdDraw = GDPA(vkCmdDraw);
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass = GDPA(vkCmdEndRenderPass);
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = GDPA(vkCmdCopyImageToBuffer);
    PFN_vkEndCommandBuffer vkEndCommandBuffer = GDPA(vkEndCommandBuffer);
    PFN_vkQueueSubmit vkQueueSubmit = GDPA(vkQueueSubmit);
    PFN_vkQueueWaitIdle vkQueueWaitIdle = GDPA(vkQueueWaitIdle);

    VkQueue queue;
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    VkPhysicalDeviceMemoryProperties memprops;
    vkGetPhysicalDeviceMemoryProperties(pd, &memprops);

    const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;

    /* Offscreen color image. */
    VkImageCreateInfo imgci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
        .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage image;
    VK_CHECK(vkCreateImage(dev, &imgci, NULL, &image));

    VkMemoryRequirements imreq;
    vkGetImageMemoryRequirements(dev, image, &imreq);
    VkMemoryAllocateInfo imai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imreq.size,
        .memoryTypeIndex = find_mem_type(&memprops, imreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    VkDeviceMemory imgmem;
    VK_CHECK(vkAllocateMemory(dev, &imai, NULL, &imgmem));
    VK_CHECK(vkBindImageMemory(dev, image, imgmem, 0));

    VkImageViewCreateInfo ivci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkImageView view;
    VK_CHECK(vkCreateImageView(dev, &ivci, NULL, &view));

    /* Render pass: clear -> store, end in TRANSFER_SRC for the copy. */
    VkAttachmentDescription att = { .format = fmt, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ref };
    VkRenderPassCreateInfo rpci = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub };
    VkRenderPass rp;
    VK_CHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp));

    VkFramebufferCreateInfo fbci = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = rp, .attachmentCount = 1, .pAttachments = &view,
        .width = W, .height = H, .layers = 1 };
    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(dev, &fbci, NULL, &fb));

    /* Shaders. */
    VkShaderModuleCreateInfo vsci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(quad_vert_spv), .pCode = quad_vert_spv };
    VkShaderModuleCreateInfo fsci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(quad_frag_spv), .pCode = quad_frag_spv };
    VkShaderModule vsm, fsm;
    VK_CHECK(vkCreateShaderModule(dev, &vsci, NULL, &vsm));
    VK_CHECK(vkCreateShaderModule(dev, &fsci, NULL, &fsm));

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vsm, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fsm, .pName = "main" },
    };

    VkPipelineVertexInputStateCreateInfo vin = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP };
    VkViewport vp = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { W, H } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));

    VkGraphicsPipelineCreateInfo gpci = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vin,
        .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs,
        .pMultisampleState = &ms, .pColorBlendState = &cb, .layout = layout, .renderPass = rp };
    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));

    /* Host-visible readback buffer. */
    VkDeviceSize bufsize = (VkDeviceSize)W * H * 4;
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufsize, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer buf;
    VK_CHECK(vkCreateBuffer(dev, &bci, NULL, &buf));
    VkMemoryRequirements bufreq;
    vkGetBufferMemoryRequirements(dev, buf, &bufreq);
    VkMemoryAllocateInfo bai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bufreq.size,
        .memoryTypeIndex = find_mem_type(&memprops, bufreq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VkDeviceMemory bufmem;
    VK_CHECK(vkAllocateMemory(dev, &bai, NULL, &bufmem));
    VK_CHECK(vkBindBufferMemory(dev, buf, bufmem, 0));

    /* Record + submit. */
    VkCommandPoolCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = qfam };
    VkCommandPool pool;
    VK_CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));

    VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cbbi));

    VkClearValue clear = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
    VkRenderPassBeginInfo rpbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = rp, .framebuffer = fb, .renderArea = { { 0, 0 }, { W, H } },
        .clearValueCount = 1, .pClearValues = &clear };
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 4, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    /* Image is already TRANSFER_SRC_OPTIMAL (render pass finalLayout). Copy. */
    VkBufferImageCopy region = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { W, H, 1 } };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmd };
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(dev, bufmem, 0, bufsize, 0, &mapped));
    const uint8_t *c = &((const uint8_t *)mapped)[(H / 2 * W + W / 2) * 4];
    printf("[Vulkan] center pixel RGBA = %d,%d,%d,%d\n", c[0], c[1], c[2], c[3]);
    int ok = near_expected(c);

    if (!ok) { fprintf(stderr, "[Vulkan] FAILED: unexpected pixel\n"); return 1; }
    printf("[Vulkan] PASS: quad rendered offscreen and verified\n");
    return 0;
}

/* ========================================================================== */

int main(int argc, char **argv)
{
    /* Unbuffered so the last line printed before any crash is visible in CI. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char *lib = argc > 1 ? argv[1] : "./libsoftpipe_gl.so";
    printf("== softpipe-gl smoke test ==\nloading %s\n", lib);

    lib_t h = load_lib(lib);
    if (!h) { fprintf(stderr, "load failed: %s\n", lib_err()); return 2; }

    p_eglGetProcAddress = (PFN_eglGetProcAddress)lib_sym(h, "eglGetProcAddress");
    p_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)lib_sym(h, "vkGetInstanceProcAddr");
    if (!p_eglGetProcAddress || !p_vkGetInstanceProcAddr) {
        fprintf(stderr, "FAILED: expected exports missing (egl=%p vk=%p)\n",
                (void *)p_eglGetProcAddress, (void *)p_vkGetInstanceProcAddr);
        return 2;
    }
    printf("resolved eglGetProcAddress=%p vkGetInstanceProcAddr=%p\n\n",
           (void *)p_eglGetProcAddress, (void *)p_vkGetInstanceProcAddr);

    int rc = 0;
    rc |= run_gl_test();
    printf("\n");
    rc |= run_vk_test();

    printf("\n== %s ==\n", rc == 0 ? "ALL PASSED" : "FAILURE");
    return rc;
}
