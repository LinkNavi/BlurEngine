game_sources = files(
  'src/main.cpp',
  'src/player.cpp',
  'src/debug/noise.cpp',
  'src/debug/track.cpp',
)

executable('blur',
  game_sources,
  dependencies: [engine_dep],
  cpp_args: ['-DGLFW_INCLUDE_NONE'])
