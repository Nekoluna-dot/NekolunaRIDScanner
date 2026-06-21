idf_component_register(
    SRCS
        main.c
        rid_config.c
        encode_gb42590.c
        rid_wifi.c
        crid_patrol.c
        crid_cli.c
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        driver
        esp_driver_uart
        nvs_flash
        esp_common
        esp_netif
        esp_event
        esp_timer
    PRIV_REQUIRES soc)

target_link_libraries(${COMPONENT_LIB} m)
