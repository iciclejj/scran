font_python = import('python').find_installation(
  'python3',
  modules: ['fontTools'],
)

generated_font = custom_target(
  'generated-font',
  input: files(
    'IosevkaFixed-Regular.ttf.xz',
    'SymbolsNerdFontMono-Regular.ttf.xz',
  ),
  depend_files: files('scripts/ui_strings_list.py'),
  output: [
    'ScranUI-Regular.ttf',
    'scran-font.c',
    'scran-font.h',
    'ui-strings.h',
  ],
  command: [
    font_python,
    '-B',
    files('scripts/generate_font.py'),
    '--iosevka-font', '@INPUT0@',
    '--nerd-font', '@INPUT1@',
    '--outpath-ttf', '@OUTPUT0@',
    '--outpath-c-source', '@OUTPUT1@',
    '--outpath-c-header', '@OUTPUT2@',
    '--outpath-ui-strings-header', '@OUTPUT3@',
  ],
)

font_source = generated_font[1]
font_header = generated_font[2]
ui_strings_header = generated_font[3]

install_data(
  'Iosevka.license',
  'NerdFontsSymbolsOnly.license',
  install_dir: get_option('datadir') / 'licenses' / meson.project_name(),
)
