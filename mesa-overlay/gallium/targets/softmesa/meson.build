# Combined single-file target: llvmpipe (GL/EGL) + lavapipe (Vulkan).
#
# Injected into Mesa's build via mesa-overlay (see build/apply-overlay.sh). It
# links the *static* internal libraries of both the GL/EGL stack and lavapipe
# into ONE shared object exposing only eglGetProcAddress + vkGetInstanceProcAddr.
#
# The GL/EGL stack differs per platform: on Linux/*nix it is the Gallium DRI
# frontend linked into libEGL; on Windows it is the WGL stack behind egl_wgl.

if host_machine.system() == 'windows'

  # ---- Windows: EGL-over-WGL (egl_wgl.c) + libgallium_wgl + lavapipe ----
  # Exports are controlled by a .def (only the two entry points are listed;
  # providing a .def disables MinGW auto-export). The MinGW runtime is linked
  # statically so the DLL depends only on system DLLs + LLVM (static).
  softmesa_link_args = [
    ld_args_gc_sections,
    '-static', '-static-libgcc', '-static-libstdc++',
  ]

  libsoftmesa = shared_library(
    'softmesa',
    # wgl_stw_init.c is the WGL target's wgl.c (copied in by apply-overlay.sh);
    # its DllMain calls stw_init(&stw_winsys) to bring up the GDI/llvmpipe WGL
    # state tracker that egl_wgl's eglInitialize depends on.
    files('softmesa.c', 'wgl_stw_init.c'),
    vs_module_defs : 'softmesa.def',
    objects : libegl.extract_all_objects(recursive : false),
    include_directories : [
      inc_include, inc_src, inc_mesa, inc_gallium, inc_gallium_aux, inc_util,
      inc_gallium_winsys, inc_gallium_winsys_sw, inc_gallium_drivers, inc_wgl,
    ],
    link_whole : [ libwgl, liblavapipe_st ],
    link_with : [
      libgallium, libglsl, libmesa, libwsgdi, libglapi_bridge, libglapi,
      libgalliumvl_stub,
      # lavapipe's software pipe-loader + sw winsys backends.
      libpipe_loader_static, libws_null, libwsw,
    ],
    dependencies : [
      dep_llvm, driver_swrast, idep_mesautil, idep_nir, dep_ws2_32,
      # MinGW's libregex (systre) -> libtre -> libintl -> libiconv chain.
      # Linked statically (-static), so these add no DLL dependency.
      cc.find_library('tre', required : false),
      cc.find_library('intl', required : false),
      cc.find_library('iconv', required : false),
    ],
    link_args : softmesa_link_args,
    name_prefix : '',
    install : true,
  )

else

  # ---- Linux / *nix / macOS: Gallium DRI frontend + libEGL + lavapipe ----
  # (On macOS the surfaceless EGL/dri stack is unlocked by vendored libdrm
  #  headers + forcing system_has_kms_drm; see build/build-macos.sh.)
  if host_machine.system() == 'darwin'
    softmesa_exp = meson.current_source_dir() / 'softmesa.exported_symbols'
    softmesa_link_args = [
      '-Wl,-dead_strip',
      '-Wl,-exported_symbols_list,@0@'.format(softmesa_exp),
    ]
    softmesa_link_depends = files('softmesa.exported_symbols')
  else
    softmesa_sym = meson.current_source_dir() / 'softmesa.sym'
    softmesa_link_args = [
      ld_args_gc_sections,
      '-Wl,--version-script,@0@'.format(softmesa_sym),
      '-Wl,--exclude-libs,ALL',
    ]
    softmesa_link_depends = files('softmesa.sym')
  endif

  # drm_stubs.c intentionally defines symbols with no header prototype.
  softmesa_c_args = cc.get_supported_arguments(['-Wno-missing-prototypes'])

  libsoftmesa = shared_library(
    'softmesa',
    files('softmesa.c', 'drm_stubs.c'),
    c_args : softmesa_c_args,
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
      # lavapipe's Vulkan runtime/wsi. Explicit because macOS ld doesn't get
      # them via liblavapipe_st's transitive link deps the way GNU ld does.
      libvulkan_runtime, libvulkan_lite_runtime, libvulkan_instance,
      libvulkan_wsi,
    ],
    dependencies : [
      dep_llvm, driver_swrast, idep_mesautil, dep_libdrm, dep_thread,
      idep_xmlconfig, dep_dl, dep_clock, dep_m,
    ],
    gnu_symbol_visibility : 'hidden',
    link_args : softmesa_link_args,
    link_depends : softmesa_link_depends,
    # Vulkan entrypoints are declared __attribute__((weak)); on Mach-O an
    # undefined weak errors unless undefined symbols are allowed (ELF resolves
    # them to 0). lavapipe's own target does the same on darwin.
    override_options : host_machine.system() == 'darwin' ? ['b_lundef=false'] : [],
    install : true,
    name_prefix : 'lib',
  )

endif
