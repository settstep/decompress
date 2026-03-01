# decompress

Kleines Konsolenprogramm in C mit CMake zum Einlesen und Entpacken einer ZIP-Datei.

## Eigenschaften

- Datei wird **byteweise** mit `fgetc` gelesen.
- Parser-Funktionen für:
  - Local File Record
  - Central Directory Record
  - End of Central Directory Record
- Wenn Strukturen oder Signaturen nicht wie erwartet sind, wird ein passender Fehlercode zurückgegeben.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Aufruf

```bash
./decompress [-debug] <zip-datei>
```

unter Windows z. B.:

```powershell
.\build\decompress.exe .\test.zip
.\build\decompress.exe -debug .\test.zip
```

## Debug-Modus

- Mit `-debug` wird im aktuellen Arbeitsverzeichnis die Datei `debug.log` erzeugt.
- In `debug.log` werden die Schritte des Programms (Einlesen, Record-Parsing, Entpacken, Fehlerursachen) protokolliert.

## Rückgabecodes (`enum ZipStatus`)

- `ZIP_OK` = Erfolg
- `ZIP_ERR_INVALID_ARG` = Ungültige Parameter
- `ZIP_ERR_IO` = Datei-/I/O-Fehler
- `ZIP_ERR_EOF` = Unerwartetes Dateiende
- `ZIP_ERR_BAD_SIGNATURE` = Falsche ZIP-Signatur
- `ZIP_ERR_BAD_FORMAT` = Ungültiges ZIP-Format
- `ZIP_ERR_UNSUPPORTED` = Nicht unterstützte ZIP-Variante
- `ZIP_ERR_MEMORY` = Speicherfehler
- `ZIP_ERR_DECOMPRESS` = Dekomprimierungsfehler

## Hinweis

Aktuell wird nur `compression_method = 0` (STORE, unkomprimiert) entpackt.
Für andere Verfahren (z. B. DEFLATE) liefert das Programm `ZIP_ERR_UNSUPPORTED`.
