# ============================================================================
# PC simulator for the speed-limit display
#
# Build and run:
#     cmake -S sim -B build-sim
#     cmake --build build-sim -j
#     ./build-sim/tempolimit-sim
#
# SDL2 is located via find_package resp. pkg-config - no hand-entered
# Homebrew paths needed, works the same on Apple Silicon, Intel Macs, and
# Linux.
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(tempolimit-sim C)

set(CMAKE_C_STANDARD 11)
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------------------
# lv_conf.h: the same file the firmware uses.
# LV_CONF_INCLUDE_SIMPLE plus a global search path is the approach LVGL
# itself uses in its example projects.
# ---------------------------------------------------------------------------
if(NOT EXISTS ${PROJ_ROOT}/include/lv_conf.h)
  message(FATAL_ERROR
    "include/lv_conf.h is missing.\n"
    "Run 'pio run -e esp32s3' once first, then "
    "'bash tools/setup_lvconf.sh'.")
endif()

# LVGL's CMake looks for lv_conf.h next to this file - sim/lv_conf.h
# forwards via #include to include/lv_conf.h, so there's only one
# configuration. LV_CONF_PATH is not honored here as of LVGL 9.5.
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)

# Enable SDL only for the simulator. include/lv_conf.h sets LV_USE_SDL via
# #ifndef only when it isn't already defined externally - this flag has to
# be set BEFORE add_subdirectory/FetchContent so it also takes effect while
# compiling the LVGL library itself (not just the simulator's own main
# program). The firmware never gets this flag and stays at LV_USE_SDL 0.
add_compile_definitions(LV_USE_SDL=1)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)

include_directories(BEFORE ${PROJ_ROOT}/include ${PROJ_ROOT}/src)

# ---------------------------------------------------------------------------
# LVGL: prefer the copy PlatformIO has already fetched.
# Otherwise download it here once.
# ---------------------------------------------------------------------------
set(LVGL_LOCAL ${PROJ_ROOT}/.pio/libdeps/esp32s3/lvgl)

if(EXISTS ${LVGL_LOCAL}/lvgl.h)
  message(STATUS "LVGL: using ${LVGL_LOCAL}")
  add_subdirectory(${LVGL_LOCAL} ${CMAKE_BINARY_DIR}/lvgl)
else()
  message(STATUS "LVGL: downloading")
  include(FetchContent)
  FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(lvgl)
endif()

# ---------------------------------------------------------------------------
# Find SDL2 - the bundled CMake package first, pkg-config as a fallback
# ---------------------------------------------------------------------------
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SDL2_TARGET PkgConfig::SDL2)
  message(STATUS "SDL2 found via pkg-config")
else()
  if(TARGET SDL2::SDL2)
    set(SDL2_TARGET SDL2::SDL2)
  else()
    set(SDL2_TARGET ${SDL2_LIBRARIES})
    include_directories(${SDL2_INCLUDE_DIRS})
  endif()
  message(STATUS "SDL2 found: ${SDL2_INCLUDE_DIRS}")
endif()

# ---------------------------------------------------------------------------
# Simulator
# ---------------------------------------------------------------------------
add_executable(tempolimit-sim
  ${CMAKE_CURRENT_SOURCE_DIR}/sim_main.c
  ${PROJ_ROOT}/src/ui.c
  ${PROJ_ROOT}/src/lv_font_din_197.c
  ${PROJ_ROOT}/src/lv_font_din_171.c
  ${PROJ_ROOT}/src/lv_font_din_56.c
  ${PROJ_ROOT}/src/lv_font_din_m205.c
  ${PROJ_ROOT}/src/lv_font_din_m162.c
  ${PROJ_ROOT}/src/lv_font_din_m48.c
  ${PROJ_ROOT}/src/lv_font_din_m72.c
  ${PROJ_ROOT}/src/lv_font_din_m205t.c
  ${PROJ_ROOT}/src/lv_font_din_m162t.c
  ${PROJ_ROOT}/src/lv_font_din_197t.c
  ${PROJ_ROOT}/src/lv_font_din_171t.c
  ${PROJ_ROOT}/src/lv_font_din_88.c
  ${PROJ_ROOT}/src/lv_font_mono16.c
  ${PROJ_ROOT}/src/img_fahrrad.c
  ${PROJ_ROOT}/src/img_spielstrasse.c)

target_link_libraries(tempolimit-sim PRIVATE lvgl ${SDL2_TARGET} m)
target_compile_definitions(tempolimit-sim PRIVATE USE_DIN_FONT)

# On macOS the SDL headers live under SDL2/, on Linux sometimes directly -
# lv_conf.h expects <SDL2/SDL.h>, so include the parent directory too.
if(SDL2_INCLUDE_DIRS)
  foreach(dir ${SDL2_INCLUDE_DIRS})
    get_filename_component(parent ${dir} DIRECTORY)
    target_include_directories(tempolimit-sim PRIVATE ${parent})
    target_include_directories(lvgl PRIVATE ${parent})
  endforeach()
endif()
