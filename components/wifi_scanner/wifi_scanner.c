#include "include/wifi_scanner.h"

void wifi_init_station(void) {
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wicfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
}

wifi_ap_record_t *wifi_scan_once(uint16_t *ap_count_out) {
  uint16_t ap_count = 0;

  wifi_scan_config_t scan_cfg = {
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
      .scan_time.active.min = 100,
      .scan_time.active.max = 300,
  };
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  if (ap_count == 0) {
    *ap_count_out = 0;
    return NULL;
  }

  wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
  if (!records) {
    *ap_count_out = 0;
    return NULL;
  }

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, records));

  *ap_count_out = ap_count;
  return records;
}
