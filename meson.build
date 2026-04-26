project('wayland-test', 'c', default_options: ['c_std=c11'])

wl_dep = dependency('wayland-client')
wl_mod = import('wayland')

gamma_xml = files('wlr-protocols/unstable/wlr-gamma-control-unstable-v1.xml')
gamma = wl_mod.scan_xml(gamma_xml)

executable('main', 'src/main.c', gamma, dependencies: wl_dep)