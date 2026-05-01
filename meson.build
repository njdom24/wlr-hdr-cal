project('wayland-test', 'c', default_options: ['c_std=gnu11'])

src = files(
  'src/main.c',
  'src/config.c',
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

gamma_xml = files('vendor/wlr-protocols/unstable/wlr-gamma-control-unstable-v1.xml')
gamma = wl_mod.scan_xml(gamma_xml)

executable('main', src, gamma, dependencies: [wl_dep, toml_dep, meson.get_compiler('c').find_library('m')])