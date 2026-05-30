scranrot_inc = include_directories('include')

scranrot_lib = static_library(
  'scranrot',
  files(
    'src/scranrot.c',
    'src/fallback.c',
    'src/fallback-yuv.c',
    'src/ssse3.c',
    'src/ssse3-yuv.c',
  ),
  include_directories: scranrot_inc
)
scranrot_dep = declare_dependency(
  link_with: scranrot_lib,
  include_directories: scranrot_inc
)
