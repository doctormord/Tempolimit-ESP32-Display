# ============================================================================
# PC-Simulator der Tempolimit-Anzeige
#
# Bauen und starten:
#     cmake -S sim -B build-sim
#     cmake --build build-sim -j
#     ./build-sim/tempolimit-sim
#
# SDL2 wird ueber find_package bzw. pkg-config gesucht - keine haendisch
# eingetragenen Homebrew-Pfade noetig, funktioniert auf Apple Silicon,
# Intel-Macs und Linux gleichermassen.
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(tempolimit-sim C)

set(CMAKE_C_STANDARD 11)
set(PROJ_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------------------
# lv_conf.h: dieselbe Datei wie die Firmware benutzt.
# LV_CONF_INCLUDE_SIMPLE + globaler Suchpfad ist der Weg, den LVGL selbst
# in seinen Beispielprojekten geht.
# ---------------------------------------------------------------------------
if(NOT EXISTS ${PROJ_ROOT}/include/lv_conf.h)
  message(FATAL_ERROR
    "include/lv_conf.h fehlt.\n"
    "Erst einmal 'pio run -e esp32s3' laufen lassen, dann "
    "'bash tools/setup_lvconf.sh'.")
endif()

# LVGLs CMake sucht lv_conf.h neben dieser Datei - sim/lv_conf.h leitet
# per #include auf include/lv_conf.h weiter, damit es nur eine Konfiguration
# gibt. LV_CONF_PATH wird von LVGL 9.5 an dieser Stelle nicht ausgewertet.
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)

# SDL nur fuer den Simulator einschalten. include/lv_conf.h setzt
# LV_USE_SDL per #ifndef nur, wenn es nicht schon von aussen definiert ist -
# dieses Flag muss VOR add_subdirectory/FetchContent gesetzt sein, damit es
# auch beim Uebersetzen der LVGL-Bibliothek selbst greift (nicht nur im
# Simulator-Hauptprogramm). Die Firmware bekommt dieses Flag nie und bleibt
# bei LV_USE_SDL 0.
add_compile_definitions(LV_USE_SDL=1)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)

include_directories(BEFORE ${PROJ_ROOT}/include ${PROJ_ROOT}/src)

# ---------------------------------------------------------------------------
# LVGL: bevorzugt die Kopie, die PlatformIO ohnehin schon geladen hat.
# Sonst wird sie hier einmalig heruntergeladen.
# ---------------------------------------------------------------------------
set(LVGL_LOCAL ${PROJ_ROOT}/.pio/libdeps/esp32s3/lvgl)

if(EXISTS ${LVGL_LOCAL}/lvgl.h)
  message(STATUS "LVGL: benutze ${LVGL_LOCAL}")
  add_subdirectory(${LVGL_LOCAL} ${CMAKE_BINARY_DIR}/lvgl)
else()
  message(STATUS "LVGL: wird heruntergeladen")
  include(FetchContent)
  FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(lvgl)
endif()

# ---------------------------------------------------------------------------
# SDL2 finden - erst das mitgelieferte CMake-Paket, sonst pkg-config
# ---------------------------------------------------------------------------
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SDL2_TARGET PkgConfig::SDL2)
  message(STATUS "SDL2 ueber pkg-config gefunden")
else()
  if(TARGET SDL2::SDL2)
    set(SDL2_TARGET SDL2::SDL2)
  else()
    set(SDL2_TARGET ${SDL2_LIBRARIES})
    include_directories(${SDL2_INCLUDE_DIRS})
  endif()
  message(STATUS "SDL2 gefunden: ${SDL2_INCLUDE_DIRS}")
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

# Auf macOS liegen die SDL-Header unter SDL2/, unter Linux teils direkt -
# lv_conf.h erwartet <SDL2/SDL.h>, also den Elternordner mit aufnehmen.
if(SDL2_INCLUDE_DIRS)
  foreach(dir ${SDL2_INCLUDE_DIRS})
    get_filename_component(parent ${dir} DIRECTORY)
    target_include_directories(tempolimit-sim PRIVATE ${parent})
    target_include_directories(lvgl PRIVATE ${parent})
  endforeach()
endif()
