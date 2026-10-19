project('scratchpads-umbriel', 'cpp', 'c',
  version: '0.1.0',
  meson_version: '>=1.0.0',
  default_options: [
    'cpp_std=c++20',
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
  ])

gtkmm_dep = dependency('gtkmm-4.0', version: '>=4.10')
layershell_dep = dependency('gtk4-layer-shell-0', version: '>=1.0')
json_dep = dependency('nlohmann_json')
toml_dep = dependency('tomlplusplus')
wayland_dep = dependency('wayland-client')
wayland_protocols_dep = dependency('wayland-protocols', version: '>=1.36')

wayland_scanner = find_program('wayland-scanner')
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

# Single source of truth for the version string (also used by --version).
version_conf = configuration_data()
version_conf.set_quoted('SPM_VERSION', meson.project_version())
configure_file(output: 'spm_config.hpp', configuration: version_conf)

# ext-image-copy-capture-v1 lets us grab the pixels of a single toplevel - even
# one hidden inside a scratchpad. The identifiers it hands out match the window
# ids Umbriel reports over IPC.
protocol_specs = [
  ['ext-foreign-toplevel-list-v1',
   'staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'],
  ['ext-image-capture-source-v1',
   'staging/ext-image-capture-source/ext-image-capture-source-v1.xml'],
  ['ext-image-copy-capture-v1',
   'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'],
]

protocol_sources = []
foreach spec : protocol_specs
  xml = wayland_protocols_dir / spec[1]
  protocol_sources += custom_target(spec[0] + '-client-header',
    input: xml,
    output: spec[0] + '-client.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_sources += custom_target(spec[0] + '-private-code',
    input: xml,
    output: spec[0] + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

protocol_lib = static_library('umbriel-protocols', protocol_sources,
  dependencies: wayland_dep,
  # wayland-scanner output is not warning-clean; our own code still is.
  c_args: ['-w'])

sources = files(
  'src/main.cpp',
  'src/model/types.cpp',
  'src/model/scratchpad.cpp',
  'src/model/config_store.cpp',
  'src/umbriel/ipc.cpp',
  'src/umbriel/capture.cpp',
  'src/ui/theme.cpp',
  'src/ui/overlay_window.cpp',
)

executable('scratchpads-umbriel', sources,
  dependencies: [gtkmm_dep, layershell_dep, json_dep, toml_dep, wayland_dep],
  link_with: protocol_lib,
  include_directories: [include_directories('src'), include_directories('.')],
  install: true)
