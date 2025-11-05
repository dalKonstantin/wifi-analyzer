#ifndef UTILS_H
#define UTILS_H
#include "esp_err.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <stdio.h>

void app_init_system(void);
void wifi_print_result(uint16_t ap_count, wifi_ap_record_t *ap_records);

#endif // !UTILS_H
