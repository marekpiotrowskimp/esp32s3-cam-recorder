# esp32s3-cam-recorder

Firmware for the **Seeed XIAO ESP32-S3 Sense** (OV3660 camera): live view over Wi-Fi and JPEG frame recording to a microSD card.

## HTTP API

| Endpoint | Description |
|---|---|
| `GET /` | list of endpoints |
| `GET /stream` | MJPEG stream |
| `GET /snap` | single JPEG snapshot |
| `GET /status` | device status |
| `POST /record/start` / `POST /record/stop` | start/stop recording frames to SD |
| `POST /capture` | save one frame to SD |

## Build

1. Copy `firmware/secrets.h.example` to `firmware/secrets.h` and fill in your Wi-Fi credentials (the file is git-ignored).
2. Arduino IDE / `arduino-cli`, board `XIAO_ESP32S3`, PSRAM: OPI.

Specification (in Polish): [spy-esp.md](spy-esp.md). Audio recording is planned after the video + SD MVP.

## License

MIT — see [LICENSE](LICENSE).
