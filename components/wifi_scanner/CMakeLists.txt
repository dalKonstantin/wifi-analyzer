idf_component_register(
  SRCS
    "wifi_scanner.c"
  INCLUDE_DIRS
    "include"        # public headers
  PRIV_INCLUDE_DIRS
    "."              # если есть приватные .h в корне компонента
  REQUIRES
    esp_wifi
    esp_event
    esp_netif
    nvs_flash
  # PRIV_REQUIRES  # если зависимости нужны только внутри компонента
)
