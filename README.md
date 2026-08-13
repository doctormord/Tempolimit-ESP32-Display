# Tempolimit-Anzeige — Einrichtung

## 1. Werkzeuge installieren

**VS Code** installieren, darin die Erweiterung **PlatformIO IDE** aus dem
Marketplace hinzufügen. Beim ersten Start lädt sie ihre Toolchain selbst
nach, das dauert ein paar Minuten.

Alternativ ohne VS Code, nur Kommandozeile:

```bash
pip install platformio
```

Für den PC-Simulator zusätzlich SDL2:

```bash
sudo apt install libsdl2-dev     # Debian/Ubuntu
brew install sdl2                # macOS
```

## 2. Projektordner anlegen

```
tempolimit/
├── platformio.ini
├── include/
│   └── lv_conf.h            <- aus LVGL kopieren, siehe Schritt 3
├── src/
│   ├── main.cpp             <- tempolimit_s3_devkit.ino, umbenannt
│   ├── ui.c
│   ├── ui.h
│   └── speedlimit_grid.h    <- Kartenlookup, liest aus LittleFS
└── sim/
    └── sim_main.c
```

Wichtig beim Umbenennen von `.ino` zu `main.cpp`: ganz oben
`#include <Arduino.h>` ergänzen. Die Arduino-IDE macht das automatisch,
PlatformIO nicht.

## 3. lv_conf.h einrichten

`include/lv_conf.h` liegt bereits fertig eingerichtet im Repo, dieser Schritt
ist normalerweise nicht nötig. Nur falls die Datei fehlt oder LVGL neu
gezogen wurde:

```bash
pio run -e esp32s3 && bash tools/setup_lvconf.sh
```

## 4. Bauen

```bash
# Firmware
pio run -e esp32s3                 # übersetzen
pio run -e esp32s3 -t upload       # flashen (USB-C an den UART-Port!)
pio device monitor                 # serielle Ausgabe, 115200 Baud

# PC-Simulator (SDL2, über CMake - NICHT über PlatformIO,
# es gibt kein [env:sim] in platformio.ini)
cmake -S sim -B build-sim && cmake --build build-sim -j
./build-sim/tempolimit-sim
```

Beim ersten Flashen des DevKit: USB-C-Kabel an den **UART-Port** (nicht den
nativen USB-Port), sonst findet PlatformIO keinen Port. Falls der Upload
scheitert, BOOT-Taste gedrückt halten, kurz RESET drücken, loslassen.

## 5. Claude Code dazu

Claude Code ist ein Kommandozeilen-Werkzeug, das im Projektordner arbeitet —
es kann Dateien lesen und ändern, `pio run` ausführen und Compilerfehler
selbst beheben. Für dieses Projekt praktisch, weil der Zyklus
„bauen → Fehler → Pin ändern → neu bauen" sonst viel Handarbeit ist.

Installation (mehrere Wege, der native Installer ist der einfachste):

```bash
# macOS/Linux
curl -fsSL https://claude.ai/install.sh | bash

# oder über npm (braucht Node.js)
npm install -g @anthropic-ai/claude-code
```

Danach im Projektordner starten:

```bash
cd tempolimit
claude
```

Beim ersten Start öffnet sich der Browser zur Anmeldung. Sinnvoll direkt
danach: `/init` ausführen — das legt eine `CLAUDE.md` an, in der der
Projektkontext steht (Board, Pinbelegung, Bau-Befehle), damit man den nicht
jedes Mal wiederholen muss.

Aktuelle Anleitung: https://code.claude.com/docs/en/setup

## 6. Erster Test ohne GPS

Die Firmware läuft auch ohne angeschlossenes GPS-Modul — sie zeigt dann das
Fragezeichen-Schild und „kein Fix" in der Statuszeile. Damit lässt sich
zuerst die Verkabelung des Displays prüfen, bevor GPS und Kartendaten
dazukommen (die liegen im Flash selbst, eine SD-Karte hat das Board nicht).

Wenn das Bild verrauscht oder versetzt ist: `LCD_QSPI_HZ` in `src/config.h`
stufenweise senken, 80 -> 40 -> 20 -> 1 MHz. Fliegende Kabel vertragen die
volle Frequenz oft nicht.
