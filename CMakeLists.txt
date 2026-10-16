cmake_minimum_required(VERSION 3.16)
project(FromDustFix C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "From Dust is a 32-bit game; build with a 32-bit (i686) toolchain.")
endif()

add_library(minhook STATIC
  external/minhook/src/buffer.c
  external/minhook/src/hook.c
  external/minhook/src/trampoline.c
  external/minhook/src/hde/hde32.c)
target_include_directories(minhook PUBLIC external/minhook/include)

add_library(FromDustFix SHARED
  src/main.cpp
  src/log.cpp
  src/config.cpp
  src/pattern.cpp
  src/borderless.cpp
  src/graphics.cpp
  src/framepacing.cpp
  src/game_hooks.cpp)
target_link_libraries(FromDustFix PRIVATE minhook winmm)
set_target_properties(FromDustFix PROPERTIES PREFIX "" SUFFIX ".asi")

if(MINGW)
  target_link_options(FromDustFix PRIVATE -static -static-libgcc -static-libstdc++)
endif()
if(MSVC)
  set_property(TARGET FromDustFix PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
