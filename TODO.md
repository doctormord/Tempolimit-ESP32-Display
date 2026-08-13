# Tempolimit-Anzeige — TODO

Diese Datei stammt aus der Einrichtungsphase vor der Umstellung auf
LittleFS und ist inzwischen abgearbeitet (Display, GPS, Kartendaten,
erste Testfahrt — alles erledigt, siehe `doc/content/history.md`).

**Offene Aufgaben stehen jetzt in `doc/content/backlog.md`.** Dort auch der
aktuelle Stand in `doc/content/handoff.md`.

## Einrichtung vom Mac ohne lokale Toolchain (weiterhin gültig)

Codespaces hat keinen USB-Zugriff — die Firmware muss also lokal aufs Board.
Ohne PlatformIO auf dem Mac geht das per Browser:

- [ ] Nach jedem Push lädt der Workflow `merged-firmware.bin` als Artifact hoch
      (Actions → build → Artifacts) — herunterladen
- [ ] In **Chrome oder Edge** (Safari kann kein WebSerial) einen
      Web-Flasher öffnen, z. B. https://esp.huhn.me
- [ ] Board per USB-C an den **UART-Port** anschließen, Adresse `0x0`,
      Datei wählen, flashen
- [ ] Falls das Board nicht erkannt wird: BOOT halten, RESET tippen, loslassen

Alternative, falls dir das zu umständlich wird: `brew install platformio`
auf dem Mac — dann `pio run -t upload` direkt, ohne Umweg.
