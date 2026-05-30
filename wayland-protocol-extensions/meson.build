fs = import('fs')

wayland_scanner = find_program(
  # wayland-scanner private-code subcommand introduced in 1.14.91
  dependency('wayland-scanner', version: '>=1.14.91', native: true).get_variable(pkgconfig: 'wayland_scanner'),
  native: true
)

wl_protocols_dir = dependency('wayland-protocols', version: '>=1.39', native: true).get_variable(pkgconfig: 'pkgdatadir')

wl_protocol_xmls = [
  wl_protocols_dir/'stable/xdg-shell/xdg-shell.xml',
  wl_protocols_dir/'stable/tablet/tablet-v2.xml',
  wl_protocols_dir/'stable/presentation-time/presentation-time.xml',
  wl_protocols_dir/'stable/viewporter/viewporter.xml',
  wl_protocols_dir/'unstable/xdg-output/xdg-output-unstable-v1.xml',
  wl_protocols_dir/'staging/cursor-shape/cursor-shape-v1.xml',
  wl_protocols_dir/'staging/ext-image-capture-source/ext-image-capture-source-v1.xml',
  wl_protocols_dir/'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml',
  wl_protocols_dir/'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml',
  wl_protocols_dir/'staging/ext-data-control/ext-data-control-v1.xml',
  wl_protocols_dir/'staging/fractional-scale/fractional-scale-v1.xml',

  # Vendored protocols
  'wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml',
  'wlr-protocols/unstable/wlr-output-management-unstable-v1.xml',
  'cosmic-protocols/unstable/cosmic-output-management-unstable-v1.xml',
  'hyprland-protocols/hyprland-surface-v1.xml',
]

wl_protocols_sources = []
wl_protocols_headers = []
foreach xml : wl_protocol_xmls
  assert(xml.endswith('.xml'), 'Expected .xml file. Got: ' + xml)
  name = fs.name(xml).substring(0, -4)

  wl_protocols_sources += custom_target(
    input: xml,
    output: name + '.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )
  wl_protocols_headers += custom_target(
    input: xml,
    output: name + '.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

wl_protocols_dep = declare_dependency(
  sources: wl_protocols_sources + wl_protocols_headers,
  include_directories: include_directories('.'),
)
