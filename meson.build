project('blur_engine', 'c', 'cpp',
  default_options: ['cpp_std=c++20', 'warning_level=2'])

glfw_dep = dependency('glfw3')
glm_dep  = dependency('glm')
dl_dep   = dependency('dl', required: false)

# glad as a small static lib (vendored)
glad_inc = include_directories('vendor/glad/include')
glad_lib = static_library('glad',
  'vendor/glad/src/glad.c',
  include_directories: glad_inc)

glad_dep = declare_dependency(
  link_with: glad_lib,
  include_directories: glad_inc)

subdir('engine')
subdir('game')
