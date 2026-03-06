gnome = import('gnome')

src_inc = include_directories('../src')

moemoji_res = gnome.compile_resources('moemoji-test-resources',
  '../src/moemoji.gresource.xml',
  source_dir: '../src',
  c_name: 'moemoji',
)

moemoji_lib = static_library('moemoji-lib',
  '../src/moemoji-window.c',
  '../src/moemoji-application.c',
  moemoji_res,
  dependencies: moemoji_deps,
  include_directories: src_inc,
)

test_moemoji = executable('test-moemoji',
  'test-moemoji.c',
  dependencies: moemoji_deps,
  include_directories: src_inc,
  link_with: moemoji_lib,
  c_args: ['-DSRCDIR="' + meson.project_source_root() + '"'],
)

schema_dir = meson.project_build_root() / 'data'

test('moemoji-unit', test_moemoji,
  env: ['GSETTINGS_SCHEMA_DIR=' + schema_dir],
)

custom_target('run-unit-tests',
  command: [test_moemoji],
  output: 'test-stamp',
  build_by_default: true,
  depends: test_moemoji,
  env: ['GSETTINGS_SCHEMA_DIR=' + schema_dir],
)
