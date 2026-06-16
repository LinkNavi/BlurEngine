engine_sources = files(
  'src/renderer.cpp',
  'src/window.cpp',
  'src/shader.cpp',
  'src/mesh.cpp',
  'src/skeleton.cpp',
)

engine_inc = include_directories('include')

sdl3_dep = dependency('sdl3')

engine_lib = static_library('engine',
  engine_sources,
  include_directories: engine_inc,
  dependencies: [glfw_dep, glm_dep, glad_dep, dl_dep, sdl3_dep],
  cpp_args: ['-DGLFW_INCLUDE_NONE'])

engine_dep = declare_dependency(
  link_with: engine_lib,
  include_directories: engine_inc,
  dependencies: [glfw_dep, glm_dep, glad_dep, sdl3_dep])
