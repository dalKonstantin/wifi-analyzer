#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "wifi_scan_res.pb-c.h"
#include <protobuf-c/protobuf-c.h>
#include <stdbool.h>
#include <string.h>

#define WIFI_SCAN_INTERVAL_MS 10000
#define WIFI_SCAN_CHANNEL_MIN 1
#define WIFI_SCAN_CHANNEL_MAX 13
#define MAX_AP_RECORDS_PER_CH 32
#define MAX_SCAN_RESULTS 256
#define BLE_DEVICE_NAME "wifi"
#define BLE_ADV_ITVL_MIN 0x0020
#define BLE_ADV_ITVL_MAX 0x0060
#define BLE_NOTIFY_HEADER_LEN 4

static const char *TAG = "wifi_scan";

static wifi_ap_record_t s_scan_buffer[MAX_SCAN_RESULTS];
static uint16_t s_scan_count = 0;
static wifi_ap_record_t s_channel_records[MAX_AP_RECORDS_PER_CH];
static uint8_t *s_serialized_buf = NULL;
static size_t s_serialized_len = 0;
static uint8_t s_own_addr_type;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_conn_mtu = 23;
static bool s_notify_enabled = false;
static uint16_t s_chr_val_handle;

static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x8a, 0x3e, 0x3b, 0x4a, 0x8c, 0x4c, 0x45, 0xa2, 0x9b,
                     0x48, 0x61, 0x6f, 0x2c, 0x8a, 0x40, 0x10);
static const ble_uuid128_t s_chr_uuid =
    BLE_UUID128_INIT(0x19, 0x34, 0x1d, 0x0a, 0x4d, 0x18, 0x44, 0xa5, 0xb7,
                     0x62, 0x5b, 0x4d, 0x22, 0xe1, 0x90, 0x21);

static const char *authmode_to_str(wifi_auth_mode_t authmode) {
  switch (authmode) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA_PSK";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2_PSK";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA_WPA2_PSK";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2_ENTERPRISE";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3_PSK";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2_WPA3_PSK";
  case WIFI_AUTH_WAPI_PSK:
    return "WAPI_PSK";
  default:
    return "UNKNOWN";
  }
}

static void wifi_scan_done_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data) {
  (void)arg;
  (void)event_base;
  (void)event_id;
  wifi_event_sta_scan_done_t *done = (wifi_event_sta_scan_done_t *)event_data;
  if (done) {
    ESP_LOGI(TAG, "scan done: status=%d, number=%d, scan_id=%d", done->status,
             done->number, done->scan_id);
  }
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (!s_serialized_buf || s_serialized_len == 0) {
    return 0;
  }

  uint16_t off = ctxt->offset;
  if (off >= s_serialized_len) {
    return BLE_ATT_ERR_INVALID_OFFSET;
  }

  size_t to_copy = s_serialized_len - off;
  int rc = os_mbuf_append(ctxt->om, s_serialized_buf + off, to_copy);
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = &s_svc_uuid.u,
     .characteristics =
         (struct ble_gatt_chr_def[]){{
                                        .uuid = &s_chr_uuid.u,
                                        .access_cb = gatt_access_cb,
                                        .flags = BLE_GATT_CHR_F_READ |
                                                 BLE_GATT_CHR_F_NOTIFY,
                                        .val_handle = &s_chr_val_handle,
                                    },
                                    {0}}},
    {0},
};

static void ble_app_advertise(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      s_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "BLE connected, handle=%d", s_conn_handle);
    } else {
      ESP_LOGI(TAG, "BLE connect failed; status=%d",
               event->connect.status);
      ble_app_advertise();
    }
    return 0;
  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "BLE disconnected; reason=%d", event->disconnect.reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    s_conn_mtu = 23;
    ble_app_advertise();
    return 0;
  case BLE_GAP_EVENT_SUBSCRIBE:
    if (event->subscribe.attr_handle == s_chr_val_handle) {
      s_notify_enabled = event->subscribe.cur_notify;
      ESP_LOGI(TAG, "BLE notify %s",
               s_notify_enabled ? "enabled" : "disabled");
    }
    return 0;
  case BLE_GAP_EVENT_MTU:
    s_conn_mtu = event->mtu.value;
    ESP_LOGI(TAG, "BLE MTU updated: %u", s_conn_mtu);
    return 0;
  default:
    return 0;
  }
}

static void ble_app_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  memset(&fields, 0, sizeof(fields));

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t *)BLE_DEVICE_NAME;
  fields.name_len = strlen(BLE_DEVICE_NAME);
  fields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
    return;
  }

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = BLE_ADV_ITVL_MIN;
  adv_params.itvl_max = BLE_ADV_ITVL_MAX;

  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                         ble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
  }
}

static void ble_app_on_sync(void) {
  int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
    return;
  }
  ble_app_advertise();
}

static void ble_app_on_reset(int reason) {
  ESP_LOGE(TAG, "BLE reset, reason=%d", reason);
}

static void ble_host_task(void *param) {
  (void)param;
  ESP_LOGI(TAG, "BLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static void app_init_system(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void wifi_init_station(void) {
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wicfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wicfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(
      esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                 &wifi_scan_done_handler, NULL));
}

static void ble_init(void) {
  int rc = nimble_port_init();
  if (rc != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
    return;
  }

  ble_svc_gap_init();
  ble_svc_gatt_init();

  rc = ble_gatts_count_cfg(s_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
    return;
  }
  rc = ble_gatts_add_svcs(s_gatt_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
    return;
  }

  rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    return;
  }

  ble_hs_cfg.sync_cb = ble_app_on_sync;
  ble_hs_cfg.reset_cb = ble_app_on_reset;

  nimble_port_freertos_init(ble_host_task);
}

static void append_scan_results(const wifi_ap_record_t *records,
                                uint16_t count) {
  for (uint16_t i = 0; i < count && s_scan_count < MAX_SCAN_RESULTS; ++i) {
    s_scan_buffer[s_scan_count++] = records[i];
  }
}

static size_t serialize_scan_results(const wifi_ap_record_t *records,
                                     uint16_t count, uint8_t **out_buf) {
  if (!records || count == 0 || !out_buf) {
    return 0;
  }

  WiFiScanResults scan_res = WI_FI_SCAN_RESULTS__INIT;
  WiFiNetwork *networks = calloc(count, sizeof(WiFiNetwork));
  WiFiNetwork **network_ptrs = calloc(count, sizeof(WiFiNetwork *));
  char **ssid_ptrs = calloc(count, sizeof(char *));
  char **auth_ptrs = calloc(count, sizeof(char *));

  if (!networks || !network_ptrs || !ssid_ptrs || !auth_ptrs) {
    free(networks);
    free(network_ptrs);
    free(ssid_ptrs);
    free(auth_ptrs);
    return 0;
  }

  for (uint16_t i = 0; i < count; ++i) {
    WiFiNetwork *net = &networks[i];
    wi_fi_network__init(net);

    size_t ssid_len = strnlen((const char *)records[i].ssid, 32);
    ssid_ptrs[i] = calloc(ssid_len + 1, sizeof(char));
    if (ssid_ptrs[i]) {
      memcpy(ssid_ptrs[i], records[i].ssid, ssid_len);
      ssid_ptrs[i][ssid_len] = '\0';
    }

    const char *auth = authmode_to_str(records[i].authmode);
    auth_ptrs[i] = strdup(auth ? auth : "UNKNOWN");

    net->ssid = ssid_ptrs[i] ? ssid_ptrs[i] : (char *)"";
    net->channel = records[i].primary;
    net->rssi = records[i].rssi;
    net->auth = auth_ptrs[i] ? auth_ptrs[i] : (char *)"UNKNOWN";

    network_ptrs[i] = net;
  }

  scan_res.n_networks = count;
  scan_res.networks = network_ptrs;

  size_t packed_len = wi_fi_scan_results__get_packed_size(&scan_res);
  if (packed_len == 0) {
    goto cleanup;
  }

  uint8_t *buf = malloc(packed_len);
  if (!buf) {
    packed_len = 0;
    goto cleanup;
  }

  wi_fi_scan_results__pack(&scan_res, buf);
  *out_buf = buf;

cleanup:
  for (uint16_t i = 0; i < count; ++i) {
    free(ssid_ptrs[i]);
    free(auth_ptrs[i]);
  }
  free(ssid_ptrs);
  free(auth_ptrs);
  free(network_ptrs);
  free(networks);

  return packed_len;
}

static void ble_send_scan_data(void) {
  if (!s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  if (!s_serialized_buf || s_serialized_len == 0) {
    return;
  }

  uint16_t mtu = s_conn_mtu ? s_conn_mtu : 23;
  uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 0;
  if (max_payload <= BLE_NOTIFY_HEADER_LEN) {
    ESP_LOGW(TAG, "BLE MTU too small for notify payload");
    return;
  }

  uint16_t chunk_size = max_payload - BLE_NOTIFY_HEADER_LEN;
  uint16_t total_len = (uint16_t)((s_serialized_len > 0xFFFF)
                                      ? 0xFFFF
                                      : s_serialized_len);
  uint16_t offset = 0;

  while (offset < total_len) {
    uint16_t remaining = total_len - offset;
    uint16_t n = (remaining > chunk_size) ? chunk_size : remaining;

    uint16_t packet_len = BLE_NOTIFY_HEADER_LEN + n;
    uint8_t *packet = malloc(packet_len);
    if (!packet) {
      ESP_LOGE(TAG, "BLE notify alloc failed");
      return;
    }

    packet[0] = (uint8_t)(total_len & 0xFF);
    packet[1] = (uint8_t)((total_len >> 8) & 0xFF);
    packet[2] = (uint8_t)(offset & 0xFF);
    packet[3] = (uint8_t)((offset >> 8) & 0xFF);
    memcpy(&packet[BLE_NOTIFY_HEADER_LEN], s_serialized_buf + offset, n);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
    free(packet);
    if (!om) {
      ESP_LOGE(TAG, "ble_hs_mbuf_from_flat failed");
      return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_chr_val_handle, om);
    if (rc != 0) {
      ESP_LOGW(TAG, "BLE notify failed: %d", rc);
      return;
    }

    offset += n;
  }
}

static void scan_all_channels_once(void) {
  s_scan_count = 0;

  for (int ch = WIFI_SCAN_CHANNEL_MIN; ch <= WIFI_SCAN_CHANNEL_MAX; ++ch) {
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = ch,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .show_hidden = true,
    };

    ESP_LOGI(TAG, "scanning channel %d", ch);
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true));

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    if (ap_count == 0) {
      continue;
    }

    if (ap_count > MAX_AP_RECORDS_PER_CH) {
      ESP_LOGI(TAG, "channel %d: %u APs, truncating to %u", ch, ap_count,
               MAX_AP_RECORDS_PER_CH);
      ap_count = MAX_AP_RECORDS_PER_CH;
    }

    memset(s_channel_records, 0, sizeof(s_channel_records));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, s_channel_records));

    for (uint16_t i = 0; i < ap_count; ++i) {
      char ssid_str[33];
      size_t ssid_len = strnlen((const char *)s_channel_records[i].ssid, 32);
      memcpy(ssid_str, s_channel_records[i].ssid, ssid_len);
      ssid_str[ssid_len] = '\0';
      ESP_LOGI(TAG, "[%2u] %-32s RSSI:%4d CH:%2d AUTH:%s", i + 1,
               ssid_str, s_channel_records[i].rssi,
               s_channel_records[i].primary,
               authmode_to_str(s_channel_records[i].authmode));
    }

    append_scan_results(s_channel_records, ap_count);
  }

  ESP_LOGI(TAG, "scan cycle done, buffered %u results", s_scan_count);

  if (s_serialized_buf) {
    free(s_serialized_buf);
    s_serialized_buf = NULL;
    s_serialized_len = 0;
  }

  s_serialized_len =
      serialize_scan_results(s_scan_buffer, s_scan_count, &s_serialized_buf);
  if (s_serialized_len > 0) {
    ESP_LOGI(TAG, "serialized %u networks into %u bytes",
             (unsigned)s_scan_count, (unsigned)s_serialized_len);
    ble_send_scan_data();
  } else {
    ESP_LOGI(TAG, "serialization skipped/failed");
  }
}

static void wifi_scan_task(void *arg) {
  (void)arg;
  while (true) {
    scan_all_channels_once();
    vTaskDelay(pdMS_TO_TICKS(WIFI_SCAN_INTERVAL_MS));
  }
}

void app_main(void) {
  app_init_system();
  wifi_init_station();
  ble_init();

  xTaskCreate(wifi_scan_task, "wifi_scan_task", 8192, NULL, 5, NULL);
}
