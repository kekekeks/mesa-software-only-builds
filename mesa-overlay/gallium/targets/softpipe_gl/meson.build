# Combined single-.so target: llvmpipe (GL/EGL) + lavapipe (Vulkan).
#
# This target is injected into Mesa's build via mesa-overlay (see
# build/apply-overlay.sh). It links the *static* internal libraries of both
# the Gallium DRI/EGL stack and lavapipe into one shared object and, together
# with the version script, exposes only eglGetProcAddress + vkGetInstanceProcAddr.

softpipe_gl_sym = meson.current_source_dir() / 'softpipe_gl.sym'

softpipe_gl_link_args = [
  ld_args_gc_sections,
  '-Wl,--version-script,@0@'.format(softpipe_gl_sym),
  '-Wl,--exclude-libs,ALL',
]

# drm_stubs.c intentionally defines symbols with no header prototype.
softpipe_gl_c_args = cc.get_supported_arguments(['-Wno-missing-prototypes'])

libsoftpipe_gl = shared_library(
  'softpipe_gl',
  files('softpipe_gl.c', 'drm_stubs.c'),
  c_args : softpipe_gl_c_args,
  # Pull in libEGL's own compiled objects (they export eglGetProcAddress).
  objects : libegl.extract_all_objects(recursive : false),
  include_directories : [
    inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux, inc_util,
    inc_gallium_winsys, inc_gallium_drivers,
    include_directories('../../frontends/dri'),
  ],
  link_whole : [ libdri, liblavapipe_st ],
  link_with : [
    libmesa, libgalliumvl, libgallium, libglapi, libpipe_loader_static,
    libws_null, libwsw, libswdri, libswkmsdri, libloader,
  ],
  dependencies : [
    dep_llvm, driver_swrast, idep_mesautil, dep_libdrm, dep_thread,
    idep_xmlconfig, dep_dl, dep_clock, dep_m,
  ],
  gnu_symbol_visibility : 'hidden',
  link_args : softpipe_gl_link_args,
  link_depends : files('softpipe_gl.sym'),
  install : true,
  name_prefix : 'lib',
)
