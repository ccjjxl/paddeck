project('scratchpads-umbriel', 'cpp',
  version: '0.1.0',
  meson_version: '>=1.0.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=release',
  ])

gtkmm_dep = dependency('gtkmm-4.0', version: '>=4.10')
layershell_dep = dependency('gtk4-layer-shell-0', version: '>=1.0')
json_dep = dependency('nlohmann_json')
toml_dep = dependency('tomlplusplus')

sources = files(
  'src/main.cpp',
  'src/model/types.cpp',
  'src/model/scratchpad.cpp',
  'src/model/config_store.cpp',
  'src/umbriel/ipc.cpp',
  'src/ui/theme.cpp',
  'src/ui/overlay_window.cpp',
)

executable('scratchpads-umbriel', sources,
  dependencies: [gtkmm_dep, layershell_dep, json_dep, toml_dep],
  include_directories: include_directories('src'),
  install: true)
