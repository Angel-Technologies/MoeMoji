application_id = 'jp.angeltech.MoeMoji'

scalable_dir = join_paths('hicolor', 'scalable', 'apps')

foreach size : ['48x48', '64x64', '128x128', '256x256']
  size_dir = join_paths('hicolor', size, 'apps')
  install_data(
    join_paths(size_dir, ('@0@.png').format(application_id)),
    install_dir: join_paths(get_option('datadir'), 'icons', size_dir)
  )
endforeach

symbolic_dir = join_paths('hicolor', 'symbolic', 'apps')
install_data(
  join_paths(symbolic_dir, ('@0@-symbolic.svg').format(application_id)),
  install_dir: join_paths(get_option('datadir'), 'icons', symbolic_dir)
)

install_data(
  join_paths(symbolic_dir, ('@0@-tray-symbolic.svg').format(application_id)),
  install_dir: join_paths(get_option('datadir'), 'icons', symbolic_dir)
)
