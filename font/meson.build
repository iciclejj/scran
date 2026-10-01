font_python = import('python').find_installation(
  'python3',
  modules: ['fontTools'],
)

generated_ui_text_and_font = custom_target(
  'generated-ui-text',
  input: files(
    'IosevkaFixed-Regular.ttf.xz',
    'SymbolsNerdFontMono-Regular.ttf.xz',
  ),
  depend_files: files('scripts/ui_strings_list.py'),
  output: [
    'ScranUI-Regular.ttf',
    'scran-ui-text.c',
    'scran-ui-text.h',
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
  ],
)

ui_text_and_font_source = generated_ui_text_and_font[1]
ui_text_and_font_header = generated_ui_text_and_font[2]

install_data(
  'Iosevka.license',
  'NerdFontsSymbolsOnly.license',
  install_dir: get_option('datadir') / 'licenses' / meson.project_name(),
)
