#!/usr/bin/env bash
# setup_lvconf.sh - create include/lv_conf.h from the LVGL template and set
# the options this project needs. Run once after the first "pio run".
set -e

TPL=$(find .pio/libdeps -name lv_conf_template.h 2>/dev/null | head -1)
if [ -z "$TPL" ]; then
  echo "LVGL hasn't been fetched yet. Run this first:  pio run -e esp32s3"
  exit 1
fi

mkdir -p include
cp "$TPL" include/lv_conf.h

# macOS needs an empty backup suffix for sed -i
if [ "$(uname)" = "Darwin" ]; then SED=(sed -i ''); else SED=(sed -i); fi

# Replace the first "#if 0", regardless of which line it's on
"${SED[@]}" '0,/^#if 0/s//#if 1/'                                          include/lv_conf.h
"${SED[@]}" 's/^#define LV_COLOR_DEPTH .*/#define LV_COLOR_DEPTH 16/'      include/lv_conf.h
"${SED[@]}" 's/^#define LV_USE_SDL .*/#define LV_USE_SDL 1/'               include/lv_conf.h
"${SED[@]}" 's/^#define LV_FONT_MONTSERRAT_48 .*/#define LV_FONT_MONTSERRAT_48 1/' include/lv_conf.h
"${SED[@]}" 's/^#define LV_MEM_SIZE .*/#define LV_MEM_SIZE (64 * 1024)/'   include/lv_conf.h

echo "include/lv_conf.h created:"
grep -nE '^#if |LV_COLOR_DEPTH|LV_USE_SDL|LV_MEM_SIZE|MONTSERRAT_48' include/lv_conf.h | head
