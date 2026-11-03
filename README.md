# esp32s3-cam-recorder

Firmware dla **Seeed XIAO ESP32-S3 Sense** (kamera OV3660): podgląd na żywo po Wi-Fi i zapis klatek JPEG na kartę microSD.

## API HTTP

| Endpoint | Opis |
|---|---|
| `GET /` | lista endpointów |
| `GET /stream` | strumień MJPEG |
| `GET /snap` | pojedyncze zdjęcie JPEG |
| `GET /status` | stan urządzenia |
| `POST /record/start` / `POST /record/stop` | zapis klatek na SD |
| `POST /capture` | zapis jednej klatki na SD |

## Build

1. Skopiuj `firmware/secrets.h.example` jako `firmware/secrets.h` i wpisz dane Wi-Fi (plik jest w `.gitignore`).
2. Arduino IDE / `arduino-cli`, płytka `XIAO_ESP32S3`, PSRAM: OPI.

Specyfikacja: [spy-esp.md](spy-esp.md).
