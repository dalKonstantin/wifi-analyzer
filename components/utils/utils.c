#include "utils.h"

void app_init_system(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
}
void wifi_print_result(uint16_t ap_count, wifi_ap_record_t *ap_records) {
  if (!ap_records) {
    printf("No AP records available\n");
    return;
  }

  const char *auth_modes[] = {"OPEN",     "WEP",           "WPA_PSK",
                              "WPA2_PSK", "WPA_WPA2_PSK",  "WPA2_ENTERPRISE",
                              "WPA3_PSK", "WPA2_WPA3_PSK", "WAPI_PSK"};

  for (int i = 0; i < ap_count; ++i) {
    int auth = ap_records[i].authmode;
    printf("[%2d] %-32s RSSI:%4d CH:%2d AUTH:%s\n", i + 1,
           (char *)ap_records[i].ssid, ap_records[i].rssi,
           ap_records[i].primary,
           (auth >= 0 && auth < 9) ? auth_modes[auth] : "UNKNOWN");
  }
}
