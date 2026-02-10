# Статус
Реализован полный `main.c` под ESP‑IDF для Wi‑Fi сканирования 2.4 ГГц.

## Что уже сделано
- Инициализация системы и Wi‑Fi в STA режиме.
- Периодическое активное сканирование каналов 1–13 в отдельной task.
- Захват SSID, RSSI, канала и authmode (строкой).
- Логи через `ESP_LOGI`.
- Буферизация результатов сканирования в `s_scan_buffer`.
- Обработчик события `WIFI_EVENT_SCAN_DONE`.
- Сериализация результатов в protobuf‑c (`WiFiScanResults`) и сохранение в буфер `s_serialized_buf`.

## Основные параметры
- Интервал сканирования: `WIFI_SCAN_INTERVAL_MS`.
- Диапазон каналов: `WIFI_SCAN_CHANNEL_MIN`…`WIFI_SCAN_CHANNEL_MAX` (1–13).
- Лимит записей на канал: `MAX_AP_RECORDS_PER_CH`.
- Общий лимит буфера: `MAX_SCAN_RESULTS`.

## Примечания
- Стек задачи увеличен до 8192, буфер канала вынесен в статическую область (`s_channel_records`) для исключения переполнения стека.
- SSID логируется через локальный `char[33]` с `\0`.
- В `scan_all_channels_once()` после цикла выполняется сериализация в protobuf и логируется размер.

## Текущие файлы
- `main/main.c`
- `main/CMakeLists.txt`
- `components/protobuf/include/wifi_scan_res.pb-c.h`
- `components/protobuf/wifi_scan_res.pb-c.c`

