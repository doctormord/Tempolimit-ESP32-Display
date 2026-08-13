#!/usr/bin/env bash
# Legt include/lv_conf.h aus der LVGL-Vorlage an und setzt die Optionen,
# die dieses Projekt braucht. Nach dem ersten "pio run" ausfuehren.
set -e

TPL=$(find .pio/libdeps -name lv_conf_template.h 2>/dev/null | head -1)
if [ -z "$TPL" ]; then
  echo "LVGL ist noch nicht geladen. Zuerst ausfuehren:  pio run -e esp32s3"
  exit 1
fi

mkdir -p include
cp "$TPL" include/lv_conf.h

# macOS braucht bei sed -i ein leeres Backup-Suffix
if [ "$(uname)" = "Darwin" ]; then SED=(sed -i ''); else SED=(sed -i); fi

# erstes "#if 0" ersetzen, egal in welcher Zeile es steht
"${SED[@]}" '0,/^#if 0/s//#if 1/'                                          include/lv_conf.h
"${SED[@]}" 's/^#define LV_COLOR_DEPTH .*/#define LV_COLOR_DEPTH 16/'      include/lv_conf.h
"${SED[@]}" 's/^#define LV_USE_SDL .*/#define LV_USE_SDL 1/'               include/lv_conf.h
"${SED[@]}" 's/^#define LV_FONT_MONTSERRAT_48 .*/#define LV_FONT_MONTSERRAT_48 1/' include/lv_conf.h
"${SED[@]}" 's/^#define LV_MEM_SIZE .*/#define LV_MEM_SIZE (64 * 1024)/'   include/lv_conf.h

echo "include/lv_conf.h angelegt:"
grep -nE '^#if |LV_COLOR_DEPTH|LV_USE_SDL|LV_MEM_SIZE|MONTSERRAT_48' include/lv_conf.h | head
