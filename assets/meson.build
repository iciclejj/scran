# TODO: Allow other archs?
if host_machine.cpu_family() != 'x86_64'
  error('Embedded font objcopy currently only supported on x86_64')
endif

# Copy the file to get predictable objcopy path-based symbol names
font_ttf = configure_file(input: 'font.ttf', output: 'font.ttf', copy: true)

font_obj = custom_target(
  'embedded-font-object',
  input: font_ttf,
  output: 'font.o',
  command: [
    find_program('objcopy'),
    '--input-target'       , 'binary',
    '--output-target'      , 'elf64-x86-64',
    '--binary-architecture', 'i386:x86-64',
    '--rename-section'     , '.data=.rodata,alloc,load,readonly,data,contents',
    '--add-section'        , '.note.GNU-stack=/dev/null',
    '--set-section-flags'  , '.note.GNU-stack=contents,readonly',
    '@INPUT@', '@OUTPUT@',
  ],
)
