project('wayland-test', 'c', default_options: ['c_std=gnu11'])

src = files(
  'src/main.c',
  'src/config.c',
  'src/outputs.c',
  'src/cm.c',
  'src/bluelight.c',
  'src/dbus.c',
)

wl_dep = dependency('wayland-client')
wl_mod = import('wayland')

toml_dep = dependency('libtomlc17', required: false)
if not toml_dep.found()
  tomlc17_lib = static_library('tomlc17',
    'vendor/tomlc17/src/tomlc17.c',
    install: false,
  )
  toml_dep = declare_dependency(
    include_directories: include_directories('vendor/tomlc17/src'),
    link_with: tomlc17_lib,
  )
endif

wlr_dep = dependency('wlr-protocols', required: false)
if wlr_dep.found()
  wlr_dir = wlr_dep.get_variable('pkgdatadir') / 'unstable/'
else
  wlr_dir = 'vendor/wlr-protocols/unstable/'
endif

gamma_xml = files(wlr_dir / 'wlr-gamma-control-unstable-v1.xml')
gamma = wl_mod.scan_xml(gamma_xml)
output_management_xml = files(wlr_dir / 'wlr-output-management-unstable-v1.xml')
output_management = wl_mod.scan_xml(output_management_xml)

wp_dep = dependency('wayland-protocols')
wp_dir = wp_dep.get_variable('pkgdatadir')
color_management_xml = files(wp_dir / 'staging/color-management/color-management-v1.xml')
color_management = wl_mod.scan_xml(color_management_xml)

dbus_dep = dependency('dbus-1')

executable('main', src, gamma, output_management, color_management,
  dependencies: [wl_dep, toml_dep, dbus_dep, meson.get_compiler('c').find_library('m')],
  c_args: ['-O2'])