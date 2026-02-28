project('moemoji', 'c',
          version: '0.1.0',
    meson_version: '>= 0.59.0',
  default_options: [ 'warning_level=2',
                     'werror=false',
                     'c_std=gnu11',
                   ],
)

i18n = import('i18n')

gnome = import('gnome')

config_h = configuration_data()
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', 'moemoji')
config_h.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('MOEMOJI_DATADIR', join_paths(get_option('prefix'), get_option('datadir'), 'moemoji'))
configure_file(
  output: 'moemoji-config.h',
  configuration: config_h,
)
add_project_arguments([
  '-I' + meson.project_build_root(),
], language: 'c')


subdir('data')
subdir('src')
subdir('tests')


gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: false,
)
