idf_component_register(
    SRCS "rl_tws.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi freertos log esp_system
)
