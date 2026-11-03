# spy-esp

## Cel projektu

Projekt `spy-esp` ma działać na `Seeed XIAO ESP32-S3 Sense` z kamerą `OV3660` i w wersji MVP realizować:

1. streaming obrazu po Wi-Fi,
2. zapis klatek JPEG na kartę microSD,
3. proste API HTTP do podglądu i diagnostyki,
4. stabilną bazę pod późniejsze dodanie audio.

Audio nie wchodzi do MVP. Zostanie dodane po uruchomieniu stabilnej wersji video + SD.

## Baza referencyjna

Punktem wyjścia jest projekt:

`rover-robot/firmware/rover_firmware`

Najważniejsze moduły referencyjne:

- `camera_module.cpp` - inicjalizacja kamery OV3660 i pobieranie klatek JPEG,
- `web_server_esp.cpp` - MJPEG stream i podstawowy serwer HTTP,
- `config.h` - konfiguracja dla `XIAO ESP32-S3 Sense`,
- `camera_pins.h` - mapowanie pinów kamery.

## Zakres MVP

### 1. Kamera
- inicjalizacja `OV3660`,
- JPEG snapshot,
- MJPEG live stream po HTTP.

### 2. HTTP API
- `GET /` - opis endpointow,
- `GET /stream` - MJPEG stream,
- `GET /snap` - pojedynczy JPEG,
- `GET /status` - status urzadzenia,
- `POST /record/start` - start zapisu snapshotow na SD,
- `POST /record/stop` - stop zapisu,
- `POST /capture` - wymuszenie zapisu jednej klatki JPEG na SD.

### 3. SD card
- montowanie karty microSD,
- tworzenie katalogu sesji,
- zapis pojedynczych klatek JPEG,
- prosty licznik zapisanych plikow.

### 4. Stabilnosc
- osobny serwer dla streamingu,
- bufory kamery w PSRAM,
- brak blokujacego, ciaglego zapisu na SD w petli streamingu.

## Swiadome uproszczenia MVP

- brak audio,
- brak synchronizacji A/V,
- brak kontenera video typu MP4,
- zapis na SD jako osobne pliki JPEG,
- zapis wyzwalany recznie lub okresowo przez prosty mechanizm firmware.

## Rekomendowana architektura MVP

### Moduly
- `config.h`
- `camera_pins.h`
- `camera_module.h/.cpp`
- `sd_storage.h/.cpp`
- `web_server_esp.h/.cpp`
- `spy_esp.ino`

### Odpowiedzialnosc modulow
- `camera_module`: start kamery i pobieranie klatek,
- `sd_storage`: montowanie SD, tworzenie sesji, zapis JPEG,
- `web_server_esp`: HTTP API, MJPEG stream, kontrola zapisu,
- `spy_esp.ino`: boot, Wi-Fi, start modulow.

## Parametry startowe MVP

- board: `XIAO ESP32S3`
- kamera: `OV3660`
- pixel format: `JPEG`
- frame size: `QVGA` na start,
- JPEG quality: `12`,
- frame buffers: `2` w PSRAM,
- stream: `MJPEG over HTTP`,
- API server: port `80`,
- stream server: port `81`.

## Kompilacja i wgrywanie

### Wymagane narzędzia

- `arduino-cli` >= 1.4.0
- core: `esp32:esp32` >= 3.3.5

### Komendy

```bash
# Kompilacja
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi firmware/

# Wgrywanie (port może się różnić)
arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi --port /dev/cu.usbmodem101 firmware/

# Kompilacja + wgrywanie w jednym
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3:PSRAM=opi --upload --port /dev/cu.usbmodem101 firmware/
```

### Krytyczna opcja: PSRAM=opi

**PSRAM musi być włączony przy kompilacji.** Bez tej flagi kamera nie zainicjuje się
(`camera_ready: false`, `camera_init_error: -1`), mimo że piny i kod są poprawne.

Domyślna wartość PSRAM w FQBN `esp32:esp32:XIAO_ESP32S3` to `disabled`.
Seeed XIAO ESP32-S3 Sense ma wbudowany OPI PSRAM 8MB — zawsze kompilować z `PSRAM=opi`.

Bez PSRAM błąd objawia się w `/status`:
```json
"camera_ready": false,
"camera_init_error": -1
```

### Port szeregowy (macOS)

Urządzenie pojawia się jako `/dev/cu.usbmodem101` (numer może być inny).
Serial Monitor działa tylko przez Arduino IDE — przez CLI (`arduino-cli monitor`) nie
działa z ESP32-S3 USB-JTAG bez dodatkowej konfiguracji.

### Diagnostyka przez HTTP

Po uruchomieniu ESP dostępne endpointy diagnostyczne:

```bash
# Sprawdzenie statusu (kamera, SD, Wi-Fi, heap)
curl http://192.168.1.80/status

# Test kamery - pojedynczy snapshot
curl http://192.168.1.80/snap --output snap.jpg

# Dashboard (pełny HTML ~17 KB)
curl http://192.168.1.80/
```

Stream MJPEG (`http://192.168.1.80:81/stream`) należy otwierać przez tag `<img>`
w przeglądarce, nie bezpośrednio jako URL — przeglądarki nie wyświetlają MJPEG
jako samodzielną stronę.

## Ryzyka techniczne

### 1. Wydajnosc
ESP32-S3 ma ograniczone zasoby, wiec zbyt wysoka rozdzielczosc lub agresywny zapis na SD moze powodowac dropy klatek.

### 2. Opoznienia SD
Zapis na microSD moze miec skokowe opoznienia. Dlatego w MVP zapisujemy pojedyncze snapshoty, a nie probujemy od razu robic ciaglego nagrywania video.

### 3. Termika
Przy pracy kamery i Wi-Fi modul moze mocno sie nagrzewac. Trzeba przewidziec testy dlugiego obciazenia oraz radiator.

### 4. Konfiguracja SD
Trzeba zweryfikowac docelowy sposob inicjalizacji slotu microSD na `XIAO ESP32-S3 Sense` w praktyce, bo to moze byc najbardziej wrazliwy element MVP.

### 5. Rozmiar HTML w httpd (znany problem, rozwiazany)
`httpd_resp_send` ma domyslny bufor ~4 KB. Strony HTML powyzej tego rozmiaru musza
byc wysylane przez `httpd_resp_send_chunk` w petli. Aktualny kod uzywa juz chunked
send dla wszystkich stron HTML (`dashboard_handler`, `sessions_page_handler`,
`session_page_handler`).

## Plan realizacji

### Etap 1. Dokumentacja i szkielet
- utworzenie `spy-esp.md`,
- przygotowanie minimalnej struktury firmware,
- przeniesienie konfiguracji kamery i serwera HTTP.

### Etap 2. Video MVP
- uruchomienie kamery,
- uruchomienie `GET /snap`,
- uruchomienie `GET /stream`,
- dodanie `GET /status`.

### Etap 3. SD MVP
- montowanie karty,
- zapis pojedynczej klatki JPEG,
- start/stop prostej sesji zapisu,
- katalogowanie plikow.

### Etap 4. Stabilizacja
- testy rozdzielczosci `QVGA` i `VGA`,
- kontrola zajecia pamieci,
- sprawdzenie stabilnosci streamingu i zapisu na SD,
- poprawki API i logow.

### Etap 5. Po MVP
- dodanie audio capture,
- dodanie audio streamingu,
- dodanie synchronizacji czasowej,
- rozwazenie lepszego formatu archiwizacji.

## Kryteria ukonczenia MVP

MVP uznajemy za gotowe, gdy firmware:

1. laczy sie z Wi-Fi,
2. udostepnia MJPEG stream,
3. zwraca pojedynczy snapshot JPEG,
4. montuje karte SD,
5. zapisuje pliki JPEG na SD,
6. udostepnia status i proste sterowanie zapisem przez HTTP.
