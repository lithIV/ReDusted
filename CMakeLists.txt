cmake_minimum_required(VERSION 3.16)
project(ReDusted C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "From Dust is a 32-bit game; build with a 32-bit (i686) toolchain.")
endif()

# Static CRT for both targets, so the .asi needs no runtime installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
  external/minhook/src/buffer.c
  external/minhook/src/hook.c
  external/minhook/src/trampoline.c
  external/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC external/minhook/include)

add_library(ReDusted SHARED
  src/main.cpp
  src/log.cpp
  src/config.cpp
  src/pattern.cpp
  src/borderless.cpp
  src/graphics.cpp
  src/framepacing.cpp
  src/game_hooks.cpp
  src/simulation.cpp
  src/intros.cpp)
target_link_libraries(ReDusted PRIVATE minhook winmm)
set_target_properties(ReDusted PROPERTIES PREFIX "" SUFFIX ".asi")

if(MINGW)
  target_link_options(ReDusted PRIVATE -static -static-libgcc -static-libstdc++)
endif()
