cmake_minimum_required(VERSION 3.16)

set(MY_PROJECT_NAME work)
project(
  ${MY_PROJECT_NAME}
  VERSION 1.0.0
)

set(CMAKE_EXPORT_COMPILE_COMMANDS true)

if(MSVC)
  set(CMAKE_CXX_STANDARD 20)
  set(MY_COMPILER_FEATURES cxx_std_20)
  add_compile_options(/utf-8 /Zc:__cplusplus)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

if(NOT MSVC)  # no MSVC o padrao C++20 ja foi definido acima
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++20" COMPILER_SUPPORTS_CXX20)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
CHECK_CXX_COMPILER_FLAG("-std=c++14" COMPILER_SUPPORTS_CXX14)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX20)
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} supports C++20")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
    set(CMAKE_CXX_STANDARD 20)
    set(MY_COMPILER_FEATURES cxx_std_20)
elseif(COMPILER_SUPPORTS_CXX17)
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} supports C++17")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
    set(CMAKE_CXX_STANDARD 17)
    set(MY_COMPILER_FEATURES cxx_std_17)
elseif(COMPILER_SUPPORTS_CXX14)
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} supports C++14")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
    set(CMAKE_CXX_STANDARD 14)
    set(MY_COMPILER_FEATURES cxx_std_14)
elseif(COMPILER_SUPPORTS_CXX11)
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} supports C++11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    set(CMAKE_CXX_STANDARD 11)
    set(MY_COMPILER_FEATURES cxx_std_11)
elseif(COMPILER_SUPPORTS_CXX0X)
     message(STATUS "The compiler ${CMAKE_CXX_COMPILER} supports C++0x")
     set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
     message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()
endif()  # NOT MSVC

set(CMAKE_VERBOSE_MAKEFILE on)

#set(CMAKE_CXX_FLAGS $ENV{CXXFLAGS})
#set(CMAKE_CXX_FLAGS_RELEASE $ENV{CXXFLAGS})
#message("CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}")
#message("CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE}")
#message("CXXFLAGS $ENV{CXXFLAGS}")

#set(CMAKE_STATIC_LINKER_FLAGS "--coverage -fprofile-arcs -ftest-coverage -lgcov")
#set(CMAKE_CXX_FLAGS "-g -O0 -Wall --coverage -fprofile-arcs -ftest-coverage")
#set(CMAKE_C_FLAGS "-g -O0 -Wall -W --coverage -fprofile-arcs -ftest-coverage")
#set(CMAKE_EXE_LINKER_FLAGS "--coverage -fprofile-arcs -ftest-coverage")

include_directories(
  ${CMAKE_SOURCE_DIR}/raylib/src
  ${CMAKE_SOURCE_DIR}/imgui
  ${CMAKE_SOURCE_DIR}/rlImGui
  ${CMAKE_SOURCE_DIR}/fmt/include
  ${CMAKE_SOURCE_DIR}/stb
  ${CMAKE_SOURCE_DIR}/json/include
  ${CMAKE_SOURCE_DIR}/work
)
#link_directories(${CMAKE_SOURCE_DIR}/BLAKE3/c/build)

set(FMT
  ${CMAKE_SOURCE_DIR}/fmt/src/format.cc
  ${CMAKE_SOURCE_DIR}/fmt/src/os.cc
)

set(IMGUI
    ${CMAKE_SOURCE_DIR}/rlImGui/rlImGui.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/imgui/imgui_demo.cpp
)
set(SRC
  main.cpp
  Task.cpp
  Core.cpp
  Utils.cpp
  Platform.cpp
)
set(HEADERS
  Const.h
  Task.h
  Core.h
  Utils.h
  Platform.h
)
set(RAYLIB_STATIC
  ${CMAKE_SOURCE_DIR}/raylib/build/raylib/libraylib.a
)
add_executable(${MY_PROJECT_NAME} ${SRC} ${FMT} ${IMGUI})
if(WIN32)
  # Windows: raylib e compilado junto (add_subdirectory no CMakeLists raiz);
  # libavif e libwebp vem do vcpkg (ver build_windows.bat).
  find_package(libavif CONFIG REQUIRED)
  find_package(WebP CONFIG REQUIRED)
  if(TARGET avif)
    set(AVIF_LIB avif)
  else()
    set(AVIF_LIB libavif::avif)
  endif()
  if(TARGET WebP::webp)
    set(WEBP_LIB WebP::webp)
  else()
    set(WEBP_LIB ${WEBP_LIBRARIES})
  endif()
  # o port libjxl do vcpkg nao instala arquivo de config do CMake (so jxl.lib + headers)
  find_library(JXL_LIB NAMES jxl REQUIRED)
  find_path(JXL_INCLUDE_DIR jxl/decode.h REQUIRED)
  target_include_directories(${MY_PROJECT_NAME} PRIVATE ${JXL_INCLUDE_DIR})
  target_link_libraries(${MY_PROJECT_NAME} PRIVATE raylib ${AVIF_LIB} ${WEBP_LIB} ${JXL_LIB})
else()
  target_link_libraries(${MY_PROJECT_NAME} ${RAYLIB_STATIC} dl pthread avif webp jxl m)
endif()
target_compile_features(${MY_PROJECT_NAME} PRIVATE ${MY_COMPILER_FEATURES})

#module with helper functions
include(gwf_module)

fix_format(${CMAKE_SOURCE_DIR}/work ${SRC} )
fix_format(${CMAKE_SOURCE_DIR}/work ${HEADERS} )
