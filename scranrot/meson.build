scranrot_inc = include_directories('include')
scranrot_sources = files(
  'src/scranrot.c',
  'src/rgba32-to-rgba32/fallback.c',
  'src/rgba32-to-rgba32/ssse3.c',
  'src/rgba32-to-yuv420/fallback.c',
  'src/rgba32-to-yuv420/ssse3.c',
)
scranrot_lib = static_library(
  'scranrot',
  scranrot_sources,
  include_directories: scranrot_inc,
)

scranrot_dep = declare_dependency(
  link_with: scranrot_lib,
  include_directories: scranrot_inc,
)

# We build a throwaway shared library just to sanity-check that scranrot doesn't
# depend on stdlib, without necessarily enforcing it for the entire project.
# Mainly just for cleanliness and to keep it more portable to other projects.
shared_library(
  'scranrot-nostdlib-test',
  scranrot_sources,
  include_directories: scranrot_inc,
  link_args: [
    '-nostdlib',
    '-Wl,--no-undefined',
    '-lgcc',
  ],
  override_options: [
    'b_ndebug=true', # Mainly to remove asserts, for now
  ],
  c_args: [
    '-fno-stack-protector',
  ],
  install: false,
  build_by_default: true,
)
