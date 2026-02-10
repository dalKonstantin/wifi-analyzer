# Wi‑Fi Analyzer (ESP‑IDF)

ESP32 application that scans 2.4 GHz Wi‑Fi networks on channels 1–13, logs results, serializes them with protobuf‑c, and sends them over BLE using NimBLE.

## Features
- STA‑mode Wi‑Fi initialization
- Active scan on channels 1–13
- Captures SSID, RSSI, channel, and auth mode
- Periodic scan task (interval configurable)
- Results buffered in RAM
- Protobuf‑c serialization
- BLE GATT server with notifications for scan results

## Requirements
- ESP‑IDF 5.5.x
- ESP32 with BLE support
- `idf.py` toolchain

## Build and Flash
```bash
. ~/esp/esp-idf/export.sh
idf.py build flash monitor
```

## BLE Interface
- Device name: `wifi`
- Service UUID: `8a3e3b4a-8c4c-45a2-9b48-616f2c8a4010`
- Characteristic UUID: `19341d0a-4d18-44a5-b762-5b4d22e19021`
- Properties: `READ` + `NOTIFY`

After subscribing to notifications, the device sends scan results after each scan cycle.

### Notification Packet Format
Each notification is a chunk with a 4‑byte header:
```
uint16_le total_len
uint16_le offset
payload bytes...
```
Concatenate payloads by `offset` to reconstruct the full protobuf message.

## Protobuf Schema
See `wifi_scan_res.proto`:
- `WifiNetwork`: `ssid`, `channel`, `rssi`, `auth`
- `WiFiScanResults`: repeated `WifiNetwork`

## Key Config
Main scan loop and BLE are implemented in:
- `main/main.c`

Scan interval and limits:
- `WIFI_SCAN_INTERVAL_MS`
- `WIFI_SCAN_CHANNEL_MIN`, `WIFI_SCAN_CHANNEL_MAX`
- `MAX_AP_RECORDS_PER_CH`, `MAX_SCAN_RESULTS`

## Notes
- When BLE is enabled, active scan uses default timing (no custom scan_time) to avoid coexistence warnings.
- If you see a flash size warning at boot, set correct flash size in `menuconfig`.

