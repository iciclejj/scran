project(
  'scran', 'c',
  version: '0.9.2',
  meson_version: '>=1.1.0',
  default_options: [
    'warning_level=2', # -Wall -Wextra
    'c_std=gnu11',
    'b_ndebug=if-release',
  ]
)

cc = meson.get_compiler('c')
add_project_arguments('-Wno-unused-parameter', language: 'c')

sd_bus_opt = get_option('sd-bus-provider')
if sd_bus_opt == 'basu'
  sd_bus_dep = dependency('basu')
elif sd_bus_opt == 'libsystemd'
  sd_bus_dep = dependency('libsystemd')
  add_project_arguments('-DSCRAN_LIBSYSTEMD_SD_BUS', language: 'c')
else
  sd_bus_dep = dependency('basu', required: false)
  if not sd_bus_dep.found()
    sd_bus_dep = dependency('libsystemd')
    add_project_arguments('-DSCRAN_LIBSYSTEMD_SD_BUS', language: 'c')
  endif
endif

blend2d_dep = dependency('blend2d', required: false)
if not blend2d_dep.found()
  warning('Meson cannot detect Blend2D as a dependency; this is expected if CMake is not installed. Falling back to header/library checks.')
  if not cc.has_header('blend2d/blend2d.h')
    error('Blend2D header not found: blend2d/blend2d.h')
  endif
  blend2d_dep = declare_dependency(
    dependencies: cc.find_library('blend2d', required: true)
  )
endif

scran_sources = files(
  'src/main.c',
  'src/capture.c',
  'src/clipboard.c',
  'src/options.c',
  'src/pipewires.c',
  'src/portals.c',
  'src/selection.c',
  'src/selection-surface.c',
  'src/signal-handlers.c',
  'src/ui.c',

  'src/event-handlers/buffer.c',
  'src/event-handlers/data-control.c',
  'src/event-handlers/fractional-scale/selection-surface.c',
  'src/event-handlers/image-copy-capture-frame/image-capture.c',
  'src/event-handlers/image-copy-capture-frame/video-capture.c',
  'src/event-handlers/image-copy-capture-session.c',
  'src/event-handlers/keyboard.c',
  'src/event-handlers/layer-surface/selection-surface.c',
  'src/event-handlers/output.c',
  'src/event-handlers/pointer.c',
  'src/event-handlers/presentation-feedback.c',
  'src/event-handlers/registry.c',
  'src/event-handlers/seat.c',
  'src/event-handlers/surface/selection.c',
  'src/event-handlers/wlr-output.c',
  'src/event-handlers/xdg-output.c',

  'src/init/capture.c',
  'src/init/datacontrol.c',
  'src/init/selection.c',

  'src/util/lib-interop.c',
  'src/util/state-util.c',
  'src/util/util.c',
)

subdir('scranrot')
subdir('wayland-protocol-extensions')
subdir('assets')

executable(
  'scran',
  scran_sources,
  include_directories: [
    include_directories('include'),
  ],
  objects: font_obj,
  dependencies: [
    cc.find_library('m', required: true),
    # ffmpeg
    dependency('libavcodec'),
    dependency('libavutil'),
    dependency('libavformat'),
    # pipewire
    dependency('libpipewire-0.3'),
    dependency('libspa-0.2'),
    # misc
    dependency('xkbcommon'),
    dependency('wayland-client'),
    sd_bus_dep,
    blend2d_dep,
    # local
    scranrot_dep,
    wl_protocols_dep,
  ],
  install: true,
)
