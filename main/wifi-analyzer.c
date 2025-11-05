#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "wifi_scan_res.pb-c.h"
#include "wifi_scanner.h"

#define CHANNELS_COUNT 13

void app_main(void) {
  wifi_ap_record_t *ap_records;
  uint16_t ap_count = 0;

  app_init_system();
  wifi_init_station();

  ap_records = wifi_scan_once(&ap_count);

  wifi_print_result(ap_count, ap_records);

  printf("\n");
  free(ap_records);
}
