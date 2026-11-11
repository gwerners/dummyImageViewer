cmake_minimum_required(VERSION 3.16)
project(all-stuff)


set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(WORK_PATH "${PROJECT_SOURCE_DIR}/work")

################################
# work
################################
if(WIN32)
  # no Linux o raylib e compilado pelo run.bash; no Windows entra no build
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  # o raylib novo desliga JPG/TGA/PNM por padrao; o viewer abre esses formatos
  set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
  foreach(f JPG TGA PNM)
    set(SUPPORT_FILEFORMAT_${f} ON CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(raylib)
endif()
add_subdirectory(work)
