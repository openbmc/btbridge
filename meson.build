project(
  'phosphor-ipmi-bt',
  'c',
  version : '1.0',
  meson_version: '>=1.1.1',
  default_options: [
    'b_lto_mode=default',
    'b_lto_threads=0',
    'b_lto=true',
    'b_ndebug=if-release',
    'warning_level=3',
    'werror=true',
  ]
)

systemd = dependency('systemd')
libsystemd = dependency('libsystemd')
systemd_system_unit_dir = systemd.get_variable(
  pkgconfig: 'systemdsystemunitdir'
)

bindir = get_option('prefix') + '/' + get_option('bindir')

executable(
  'btbridged',
  'btbridged.c',
  dependencies: [
    libsystemd,
  ],
  install: true,
  install_dir: bindir
)

configure_file(
  input: 'btbridged.service',
  output: 'org.openbmc.HostIpmi.service',
  install: true,
  install_dir: systemd_system_unit_dir,
  copy: true,
)
