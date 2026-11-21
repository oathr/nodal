cmake_minimum_required(VERSION 3.22)
project(Nodal VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# The sources are UTF-8 (preset names in French).
if(MSVC)
  add_compile_options(/utf-8)
endif()

option(NODAL_BUILD_RENDER "Build the offline render / measurement tool" ON)
option(NODAL_BUILD_PLUGIN "Build the plugin (fetches JUCE)" ON)

# DSP core: header-only, no framework dependency, shared by the tool and the plugin.
add_library(nodal_dsp INTERFACE)
target_include_directories(nodal_dsp INTERFACE dsp/include)

if(NODAL_BUILD_RENDER)
  add_executable(nodal_render tools/render/main.cpp)
  target_link_libraries(nodal_render PRIVATE nodal_dsp)
  if(MSVC)
    target_compile_options(nodal_render PRIVATE /O2)
  else()
    target_compile_options(nodal_render PRIVATE -Wall -Wextra -O3)
  endif()
endif()

if(NODAL_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
