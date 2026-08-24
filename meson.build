project(
  'scran', 'c',
  version: files('VERSION'),
  meson_version: '>=1.1.0',
  license: 'MIT AND OFL-1.1',
  license_files: ['./LICENSE', 'assets/Iosevka.license', 'assets/NerdFontsSymbolsOnly.license',],
  default_options: [
    'warning_level=2', # -Wall -Wextra
    'c_std=gnu11',
    'b_ndebug=if-release',
  ]
)

cc = meson.get_compiler('c')
add_project_arguments(
  [
    '-Wno-unused-parameter',
    '-Wmissing-prototypes',
    '-DSCRAN_VERSION_STRING="v@0@"'.format(meson.project_version()),
  ],
  language: 'c'
)

sd_bus_opt = get_option('sd-bus-provider')
if sd_bus_opt == 'basu'
  sd_bus_dep = dependency('basu')
elif sd_bus_opt == 'libsystemd'
  sd_bus_dep = dependency('libsystemd')
  add_project_arguments('-DSCRAN_LIBSYSTEMD_SD_BUS', language: 'c')
else
  sd_bus_dep = dependency('libsystemd', required: false)
  if sd_bus_dep.found()
    add_project_arguments('-DSCRAN_LIBSYSTEMD_SD_BUS', language: 'c')
  else
    sd_bus_dep = dependency('basu')
  endif
endif

blend2d_dep = dependency('blend2d', required: false)
if not blend2d_dep.found()
  message('Meson cannot detect Blend2D as a dependency; this is expected if CMake is not installed. Falling back to header/library checks.')
  if not cc.has_header('blend2d/blend2d.h')
    error('Blend2D header not found: blend2d/blend2d.h')
  endif
  blend2d_dep = declare_dependency(
    dependencies: cc.find_library('blend2d', required: true)
  )
endif

scran_sources = files(
  'src/main.c',
  'src/capture/capture.c',
  'src/capture/write/video.c',
  'src/clipboard.c',
  'src/cursor.c',
  'src/freezeframe.c',
  'src/options.c',
  'src/pipewires.c',
  'src/dbus.c',
  'src/seat.c',
  'src/selection.c',
  'src/selection-surface.c',
  'src/signal-handlers.c',
  'src/ui.c',

  'src/event-handlers/buffer.c',
  'src/event-handlers/data-control.c',
  'src/event-handlers/fractional-scale/selection-surface.c',
  'src/event-handlers/image-copy-capture-frame.c',
  'src/event-handlers/image-copy-capture-session.c',
  'src/event-handlers/keyboard.c',
  'src/event-handlers/layer-surface/selection-surface.c',
  'src/event-handlers/output.c',
  'src/event-handlers/pointer.c',
  'src/event-handlers/presentation-feedback/selection-surface.c',
  'src/event-handlers/presentation-feedback/selection-transparent-for-freezeframe.c',
  'src/event-handlers/presentation-feedback/selection-transparent-for-fullscreen-capture.c',
  'src/event-handlers/registry.c',
  'src/event-handlers/seat.c',
  'src/event-handlers/surface/selection.c',
  'src/event-handlers/wlr-output.c',
  'src/event-handlers/xdg-output.c',

  'src/init/capture.c',
  'src/init/datacontrol.c',
  'src/init/freezeframe.c',
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
