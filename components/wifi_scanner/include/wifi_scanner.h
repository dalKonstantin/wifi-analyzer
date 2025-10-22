#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdio.h>

void wifi_init_station(void);
wifi_ap_record_t *wifi_scan_once(uint16_t *ap_count_out);

#endif // !WIFI_SCANNER_H
