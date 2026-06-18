scranrot_inc = include_directories('include')
scranrot_sources = files(
  'src/scranrot.cpp',
  'src/rgba32_to_rgba32/fallback.cpp',
  'src/rgba32_to_rgba32/ssse3.cpp',
  'src/rgba32_to_yuv420/fallback.cpp',
  'src/rgba32_to_yuv420/ssse3.cpp',
)
scranrot_cpp_args = [
  '-fno-exceptions',
  '-fno-rtti',
  '-fno-threadsafe-statics',
]

scranrot_lib = static_library(
  'scranrot',
  scranrot_sources,
  include_directories: scranrot_inc,
  cpp_args: scranrot_cpp_args,
)

scranrot_dep = declare_dependency(
  link_with: scranrot_lib,
  include_directories: scranrot_inc,
)

# We build a throwaway shared library just to sanity-check that scranrot doesn't
# depend on stdlibs, without necessarily enforcing it for the entire project.
# Mainly just for cleanliness and to keep it more portable to other projects.
shared_library(
  'scranrot-nostdlib-test',
  scranrot_sources,
  include_directories: scranrot_inc,
  override_options: [
    'b_ndebug=true', # Mainly to remove asserts, for now
  ],
  cpp_args: scranrot_cpp_args + [
    '-fno-stack-protector',
  ],
  link_args: [
    '-nostdlib',
    '-Wl,--no-undefined',
    '-lgcc',
  ],
  install: false,
  build_by_default: true,
)
