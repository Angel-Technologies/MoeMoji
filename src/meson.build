moemoji_sources = [
  'main.c',
  'moemoji-window.c',
  'moemoji-application.c',

]

cc = meson.get_compiler('c')

moemoji_deps = [
  dependency('libadwaita-1', version: '>= 1.0'),
  cc.find_library('m', required: false),
]

gnome = import('gnome')

moemoji_sources += gnome.compile_resources('moemoji-resources',
  'moemoji.gresource.xml',
  c_name: 'moemoji'
)

executable('moemoji', moemoji_sources,
  dependencies: moemoji_deps,
  install: true,
)
